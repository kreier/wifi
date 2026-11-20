#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "board_pins.h"
#include "led_controller.h"
#include "app_state.h"

namespace {

struct ChannelWorkState {
    volatile uint32_t mgmt_frames;
    volatile uint32_t data_frames;
    volatile uint32_t ctrl_frames;
    volatile uint32_t rssi_bins[4];
    uint64_t total_airtime_us;
    uint8_t seen_macs[128][6];
    uint16_t unique_mac_count;
    portMUX_TYPE mux;
};

static ChannelWorkState s_chanWork = {
    0, 0, 0, {0, 0, 0, 0}, 0, {{0}}, 0, portMUX_INITIALIZER_UNLOCKED
};

static void IRAM_ATTR promiscuousRxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &pkt->rx_ctrl;

    portENTER_CRITICAL_ISR(&s_chanWork.mux);

    // 1. Classify Frame Type
    if (type == WIFI_PKT_MGMT) {
        s_chanWork.mgmt_frames++;
    } else if (type == WIFI_PKT_DATA) {
        s_chanWork.data_frames++;
    } else {
        s_chanWork.ctrl_frames++;
    }

    // 2. RSSI Histogram Binning
    // Bins: [0]: < -80 dBm, [1]: -80 to -71 dBm, [2]: -70 to -61 dBm, [3]: >= -60 dBm
    int8_t rssi = rx_ctrl->rssi;
    if (rssi < -80) {
        s_chanWork.rssi_bins[0]++;
    } else if (rssi < -70) {
        s_chanWork.rssi_bins[1]++;
    } else if (rssi < -60) {
        s_chanWork.rssi_bins[2]++;
    } else {
        s_chanWork.rssi_bins[3]++;
    }

    // 3. Frame Airtime Estimation
    uint16_t frame_len = rx_ctrl->sig_len;
    if (frame_len == 0) frame_len = 60; // minimum fallback estimate

    uint32_t rate_kbps = 24000; // default 24 Mbps
#if defined(TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32C5)
    // ESP32-C5 (802.11ax / HE) rx_ctrl has rate & sig_len, but not legacy sig_mode/mcs/sgi bitfields
    static const uint32_t legacy_rates[] = {
        1000, 2000, 5500, 11000, 6000, 9000, 12000, 18000, 24000, 36000, 48000, 54000
    };
    if (rx_ctrl->rate < 12) {
        rate_kbps = legacy_rates[rx_ctrl->rate];
    } else {
        rate_kbps = 54000;
    }
#else
    if (rx_ctrl->sig_mode == 0) {
        // Legacy 802.11a/b/g rates (kbps)
        static const uint32_t legacy_rates[] = {
            1000, 2000, 5500, 11000, 6000, 9000, 12000, 18000, 24000, 36000, 48000, 54000
        };
        if (rx_ctrl->rate < 12) {
            rate_kbps = legacy_rates[rx_ctrl->rate];
        }
    } else {
        // HT (802.11n) MCS 0..7 (20 MHz, Long Guard Interval)
        static const uint32_t ht_rates[] = {
            6500, 13000, 19500, 26000, 39000, 52000, 58500, 65000
        };
        uint8_t mcs = rx_ctrl->mcs;
        if (mcs < 8) {
            rate_kbps = ht_rates[mcs];
            if (rx_ctrl->sgi) {
                rate_kbps = (rate_kbps * 10) / 9; // Short Guard Interval bonus
            }
        }
    }
#endif

    // Overhead: Preamble & PLCP (~32 µs) + Payload duration + SIFS/ACK (~40 µs)
    uint32_t frame_airtime_us = 32 + (uint32_t)((frame_len * 8000ULL) / rate_kbps) + 40;
    s_chanWork.total_airtime_us += frame_airtime_us;

    // 4. Unique Transmitters: TA / SA extracted from bytes 10..15 in 802.11 MAC header
    if (frame_len >= 16) {
        const uint8_t *payload = pkt->payload;
        const uint8_t *ta = payload + 10;
        bool found = false;
        uint16_t count = s_chanWork.unique_mac_count;
        for (uint16_t i = 0; i < count; i++) {
            if (s_chanWork.seen_macs[i][0] == ta[0] &&
                s_chanWork.seen_macs[i][1] == ta[1] &&
                s_chanWork.seen_macs[i][2] == ta[2] &&
                s_chanWork.seen_macs[i][3] == ta[3] &&
                s_chanWork.seen_macs[i][4] == ta[4] &&
                s_chanWork.seen_macs[i][5] == ta[5]) {
                found = true;
                break;
            }
        }
        if (!found && count < 128) {
            memcpy(s_chanWork.seen_macs[count], ta, 6);
            s_chanWork.unique_mac_count++;
        }
    }

    portEXIT_CRITICAL_ISR(&s_chanWork.mux);
}

} // anonymous namespace

class ChannelAnalyzer {
public:
    static std::vector<ChannelUtilizationItem> measureChannels(
        uint32_t dwellMsPerChannel = 2000,
        LedController *led = nullptr,
        bool *bootPressed = nullptr
    ) {
        std::vector<ChannelUtilizationItem> results;
        Serial.println(F("[ChannelAnalyzer] Starting 13-channel promiscuous congestion survey..."));

        if (bootPressed) *bootPressed = false;

        // Ensure station mode and disconnect radio from AP
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        delay(50);

        s_chanWork.mux = portMUX_INITIALIZER_UNLOCKED;

        // Configure promiscuous mode with all filters enabled
        wifi_promiscuous_filter_t filter;
        filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous_rx_cb(&promiscuousRxCallback);
        esp_wifi_set_promiscuous(true);

        for (uint8_t ch = 1; ch <= 13; ch++) {
            // While scanning have the LED on
            if (led) led->setSolid(true);

            // Reset channel accumulators
            portENTER_CRITICAL(&s_chanWork.mux);
            s_chanWork.mgmt_frames = 0;
            s_chanWork.data_frames = 0;
            s_chanWork.ctrl_frames = 0;
            s_chanWork.rssi_bins[0] = 0;
            s_chanWork.rssi_bins[1] = 0;
            s_chanWork.rssi_bins[2] = 0;
            s_chanWork.rssi_bins[3] = 0;
            s_chanWork.total_airtime_us = 0;
            s_chanWork.unique_mac_count = 0;
            portEXIT_CRITICAL(&s_chanWork.mux);

            // Switch to target channel
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

            // Dwell for specified duration, polling BOOT button
            uint32_t dwellStart = millis();
            bool aborted = false;
            while (millis() - dwellStart < dwellMsPerChannel) {
                if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                    Serial.println(F("[ChannelAnalyzer] BOOT button press detected during channel scan!"));
                    if (bootPressed) *bootPressed = true;
                    aborted = true;
                    break;
                }
                delay(10);
            }

            if (aborted) {
                if (led) led->setOff();
                break;
            }

            // Capture snapshot of metrics for channel ch
            portENTER_CRITICAL(&s_chanWork.mux);
            ChannelUtilizationItem item;
            item.channel = ch;
            item.mgmt_frames = s_chanWork.mgmt_frames;
            item.data_frames = s_chanWork.data_frames;
            item.ctrl_frames = s_chanWork.ctrl_frames;
            item.rssi_hist[0] = s_chanWork.rssi_bins[0];
            item.rssi_hist[1] = s_chanWork.rssi_bins[1];
            item.rssi_hist[2] = s_chanWork.rssi_bins[2];
            item.rssi_hist[3] = s_chanWork.rssi_bins[3];
            item.unique_transmitters = s_chanWork.unique_mac_count;

            uint64_t dwellUs = (uint64_t)dwellMsPerChannel * 1000ULL;
            float airtime = ((float)s_chanWork.total_airtime_us / (float)dwellUs) * 100.0f;
            if (airtime > 100.0f) airtime = 100.0f;
            item.airtime_pct = airtime;
            portEXIT_CRITICAL(&s_chanWork.mux);

            results.push_back(item);
            Serial.printf("[ChannelAnalyzer] Ch %2d: %4u mgmt, %4u data, %4u ctrl, %2u transmitters, %5.1f%% airtime\n",
                          ch, (unsigned)item.mgmt_frames, (unsigned)item.data_frames, (unsigned)item.ctrl_frames,
                          (unsigned)item.unique_transmitters, item.airtime_pct);

            // When switching to the next channel have the LED off, maybe 100 ms - so it is not static lit
            if (ch < 13) {
                if (led) led->setOff();
                uint32_t offStart = millis();
                while (millis() - offStart < 100) {
                    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                        Serial.println(F("[ChannelAnalyzer] BOOT button press detected between channels!"));
                        if (bootPressed) *bootPressed = true;
                        aborted = true;
                        break;
                    }
                    delay(10);
                }
                if (aborted) break;
            }
        }

        // Cleanly disable promiscuous mode
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        if (led) led->setOff();
        delay(50);

        Serial.println(F("[ChannelAnalyzer] Completed 13-channel survey."));
        return results;
    }
};
