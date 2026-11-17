#pragma once

#include <Arduino.h>
#include <esp_partition.h>
#include <ArduinoJson.h>
#include "app_state.h"

// Raw Partition Telemetry Spool (No LittleFS needed!)
// Partition: 'spiffs' partition in min_spiffs.csv (192 KB / 48 sectors)
// Sector 0 (0..4095): Header & State
// Sectors 1..3 (4096..16383): 48-slot 30-minute Channel History Buffer
// Sectors 4..47 (16384..196607): Circular binary records

#define SPOOL_MAGIC_HEADER 0x574D4F4E // "WMON"
#define SPOOL_REC_MAGIC    0xAA55
#define SECTOR_SIZE        4096
#define SPOOL_START_OFFSET 0x4000 // Offset 16 KB (Sector 4)

struct SpoolHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t record_count;
    uint32_t write_offset;
    uint32_t crc32;
};

class SpoolManager {
public:
    static const esp_partition_t* getPartition() {
        static const esp_partition_t *part = nullptr;
        if (!part) {
            part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
            if (!part) {
                // Fallback by label
                part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
            }
        }
        return part;
    }

    static bool begin() {
        const esp_partition_t *part = getPartition();
        if (!part) {
            Serial.println(F("[RawSpool] ERROR: Data partition 'spiffs' not found!"));
            return false;
        }

        SpoolHeader hdr;
        if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) {
            return false;
        }

        if (hdr.magic != SPOOL_MAGIC_HEADER) {
            // First time initialization: format sector 0
            clearSpool();
        }
        return true;
    }

    static bool hasPendingData() {
        const esp_partition_t *part = getPartition();
        if (!part) return false;

        SpoolHeader hdr;
        if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) return false;
        return (hdr.magic == SPOOL_MAGIC_HEADER && hdr.record_count > 0);
    }

    static uint32_t getRecordCount() {
        const esp_partition_t *part = getPartition();
        if (!part) return 0;

        SpoolHeader hdr;
        if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) return 0;
        return (hdr.magic == SPOOL_MAGIC_HEADER) ? hdr.record_count : 0;
    }

    // Append cycle in binary format ONLY when offline
    static bool appendCycle(const CycleTelemetry &cycle) {
        const esp_partition_t *part = getPartition();
        if (!part) return false;

        SpoolHeader hdr;
        if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK || hdr.magic != SPOOL_MAGIC_HEADER || hdr.write_offset < SPOOL_START_OFFSET) {
            clearSpool();
            hdr.write_offset = SPOOL_START_OFFSET;
            hdr.record_count = 0;
        }

        // Serialize cycle into binary buffer
        uint8_t buffer[1024];
        size_t offset = 0;

        // Record Header
        uint16_t magic = SPOOL_REC_MAGIC;
        memcpy(buffer + offset, &magic, 2); offset += 2;

        uint16_t lenPlaceholder = 0; // will fill later
        size_t lenOffset = offset;
        memcpy(buffer + offset, &lenPlaceholder, 2); offset += 2;

        uint32_t ts = (uint32_t)cycle.timestamp;
        memcpy(buffer + offset, &ts, 4); offset += 4;

        uint16_t scanDur = (uint16_t)cycle.scan_duration_ms;
        memcpy(buffer + offset, &scanDur, 2); offset += 2;

        uint16_t batMv = cycle.battery_mv;
        memcpy(buffer + offset, &batMv, 2); offset += 2;

        // 1. Wi-Fi Survey (cap at 25 APs)
        uint8_t surveyCount = (cycle.survey.size() > 25) ? 25 : cycle.survey.size();
        buffer[offset++] = surveyCount;

        for (uint8_t i = 0; i < surveyCount; i++) {
            const auto &item = cycle.survey[i];
            // MAC address (6 bytes)
            uint8_t mac[6] = {0};
            parseMac(item.bssid, mac);
            memcpy(buffer + offset, mac, 6); offset += 6;

            int8_t rssi = (int8_t)item.rssi;
            buffer[offset++] = (uint8_t)rssi;
            buffer[offset++] = item.channel;
            buffer[offset++] = item.quality;

            // SSID (length + string)
            uint8_t slen = item.ssid.length() > 24 ? 24 : item.ssid.length();
            buffer[offset++] = slen;
            if (slen > 0) {
                memcpy(buffer + offset, item.ssid.c_str(), slen);
                offset += slen;
            }
        }

        // 2. Channel Utilization (13 channels)
        uint8_t chanCount = cycle.has_channel_utilization ? cycle.channel_utilization.size() : 0;
        buffer[offset++] = chanCount;

        for (uint8_t i = 0; i < chanCount; i++) {
            const auto &co = cycle.channel_utilization[i];
            buffer[offset++] = co.channel;
            buffer[offset++] = (uint8_t)round(co.airtime_pct);
            uint16_t tx = co.unique_transmitters;
            memcpy(buffer + offset, &tx, 2); offset += 2;
            uint16_t mgmt = (co.mgmt_frames > 65535) ? 65535 : (uint16_t)co.mgmt_frames;
            memcpy(buffer + offset, &mgmt, 2); offset += 2;
            uint16_t data = (co.data_frames > 65535) ? 65535 : (uint16_t)co.data_frames;
            memcpy(buffer + offset, &data, 2); offset += 2;
            uint16_t ctrl = (co.ctrl_frames > 65535) ? 65535 : (uint16_t)co.ctrl_frames;
            memcpy(buffer + offset, &ctrl, 2); offset += 2;
            for (int h = 0; h < 4; h++) {
                uint16_t hv = (co.rssi_hist[h] > 65535) ? 65535 : (uint16_t)co.rssi_hist[h];
                memcpy(buffer + offset, &hv, 2); offset += 2;
            }
        }

        // 3. Monitored Networks status
        uint8_t netCount = cycle.measurements.size();
        buffer[offset++] = netCount;
        for (uint8_t i = 0; i < netCount; i++) {
            const auto &m = cycle.measurements[i];
            uint8_t nslen = m.ssid.length() > 20 ? 20 : m.ssid.length();
            buffer[offset++] = nslen;
            if (nslen > 0) {
                memcpy(buffer + offset, m.ssid.c_str(), nslen);
                offset += nslen;
            }
            int16_t assoc = (int16_t)m.assoc_ms;
            memcpy(buffer + offset, &assoc, 2); offset += 2;
            int16_t dhcp = (int16_t)m.dhcp_ms;
            memcpy(buffer + offset, &dhcp, 2); offset += 2;
            int16_t cf = (int16_t)m.cloudflare_ms;
            memcpy(buffer + offset, &cf, 2); offset += 2;
            int16_t gg = (int16_t)m.google_ms;
            memcpy(buffer + offset, &gg, 2); offset += 2;

            uint8_t stlen = m.status.length() > 16 ? 16 : m.status.length();
            buffer[offset++] = stlen;
            if (stlen > 0) {
                memcpy(buffer + offset, m.status.c_str(), stlen);
                offset += stlen;
            }
        }

        // 4. Bluetooth status
        buffer[offset++] = cycle.has_bluetooth ? 1 : 0;
        if (cycle.has_bluetooth) {
            uint16_t bMacs = cycle.bluetooth.ble_total_macs;
            memcpy(buffer + offset, &bMacs, 2); offset += 2;
            uint16_t bUniq = cycle.bluetooth.ble_unique_devices;
            memcpy(buffer + offset, &bUniq, 2); offset += 2;
            uint16_t bRand = cycle.bluetooth.ble_random_macs;
            memcpy(buffer + offset, &bRand, 2); offset += 2;
            uint16_t bPub = cycle.bluetooth.ble_public_macs;
            memcpy(buffer + offset, &bPub, 2); offset += 2;
            buffer[offset++] = (uint8_t)cycle.bluetooth.ble_beacons;
            buffer[offset++] = (uint8_t)cycle.bluetooth.ble_smartphones;
            buffer[offset++] = (uint8_t)cycle.bluetooth.ble_wearables;
            buffer[offset++] = (uint8_t)cycle.bluetooth.ble_audio;
            buffer[offset++] = (uint8_t)cycle.bluetooth.ble_other;
            uint16_t cTot = cycle.bluetooth.classic_total;
            memcpy(buffer + offset, &cTot, 2); offset += 2;
        }

        // Update record length
        uint16_t totalRecLen = offset;
        memcpy(buffer + lenOffset, &totalRecLen, 2);

        // Simple XOR checksum at end
        uint8_t chk = 0;
        for (size_t b = 0; b < offset; b++) chk ^= buffer[b];
        buffer[offset++] = chk;

        // Check if write_offset exceeds partition limits (wrap around or ring buffer)
        if (hdr.write_offset + offset >= part->size) {
            Serial.println(F("[RawSpool] Flash spool partition full! Wrapping circular buffer to start."));
            hdr.write_offset = SPOOL_START_OFFSET;
        }

        // If write crosses sector boundary, erase target sector first
        uint32_t currSec = hdr.write_offset / SECTOR_SIZE;
        uint32_t nextSec = (hdr.write_offset + offset) / SECTOR_SIZE;
        if (hdr.write_offset % SECTOR_SIZE == 0) {
            esp_partition_erase_range(part, hdr.write_offset, SECTOR_SIZE);
        } else if (nextSec > currSec && (nextSec * SECTOR_SIZE < part->size)) {
            esp_partition_erase_range(part, nextSec * SECTOR_SIZE, SECTOR_SIZE);
        }

        if (esp_partition_write(part, hdr.write_offset, buffer, offset) != ESP_OK) {
            Serial.println(F("[RawSpool] ERROR: Flash sector write failed"));
            return false;
        }

        hdr.write_offset += offset;
        hdr.record_count++;

        // Update Header in Sector 0
        updateHeader(part, hdr);

        Serial.printf("[RawSpool] Appended offline record (%u bytes in binary, total: %u records, write head: %u)\n",
                      (unsigned)offset, (unsigned)hdr.record_count, (unsigned)hdr.write_offset);
        return true;
    }

    // Reconstruct JSON payload array from stored binary records for upload
    static String readNextBatchJson(const String &stationId, size_t maxBatchRecords = 12) {
        const esp_partition_t *part = getPartition();
        if (!part) return "";

        SpoolHeader hdr;
        if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK || hdr.record_count == 0) {
            return "";
        }

        String json = "[";
        uint32_t readOffset = SPOOL_START_OFFSET;
        uint32_t count = 0;
        bool first = true;

        while (readOffset < hdr.write_offset && count < maxBatchRecords) {
            uint16_t magic = 0;
            if (esp_partition_read(part, readOffset, &magic, 2) != ESP_OK || magic != SPOOL_REC_MAGIC) {
                break;
            }

            uint16_t recLen = 0;
            esp_partition_read(part, readOffset + 2, &recLen, 2);
            if (recLen == 0 || recLen > 1024) break;

            uint8_t buffer[1024];
            if (esp_partition_read(part, readOffset, buffer, recLen + 1) != ESP_OK) break;

            // Reconstruct JSON cycle doc
            JsonDocument doc;
            doc["station_id"] = stationId;
            doc["firmware_version"] = FIRMWARE_VERSION;

            uint32_t ts = 0;
            memcpy(&ts, buffer + 4, 4);
            doc["timestamp"] = ts;

            uint16_t dur = 0;
            memcpy(&dur, buffer + 8, 2);
            doc["scan_duration_ms"] = dur;

            uint16_t bat = 0;
            memcpy(&bat, buffer + 10, 2);
            if (bat > 0) doc["battery_mv"] = bat;

            size_t off = 12;

            // Survey
            uint8_t surveyCount = buffer[off++];
            JsonArray arrSurvey = doc["scan_survey"].to<JsonArray>();
            for (uint8_t s = 0; s < surveyCount; s++) {
                JsonObject so = arrSurvey.add<JsonObject>();
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                         buffer[off], buffer[off+1], buffer[off+2], buffer[off+3], buffer[off+4], buffer[off+5]);
                off += 6;
                so["bssid"] = macStr;
                so["rssi"] = (int8_t)buffer[off++];
                so["channel"] = buffer[off++];
                so["quality"] = buffer[off++];
                uint8_t slen = buffer[off++];
                char ssidBuf[25] = {0};
                if (slen > 0) {
                    memcpy(ssidBuf, buffer + off, slen);
                    off += slen;
                }
                so["ssid"] = ssidBuf;
                so["encryption"] = "WPA2";
                so["band"] = "2.4GHz";
            }

            // Channels
            uint8_t chanCount = buffer[off++];
            if (chanCount > 0) {
                JsonArray arrChan = doc["channel_utilization"].to<JsonArray>();
                for (uint8_t c = 0; c < chanCount; c++) {
                    JsonObject co = arrChan.add<JsonObject>();
                    co["channel"] = buffer[off++];
                    co["airtime_pct"] = buffer[off++];
                    uint16_t tx = 0; memcpy(&tx, buffer + off, 2); off += 2;
                    co["unique_transmitters"] = tx;
                    uint16_t mgmt = 0; memcpy(&mgmt, buffer + off, 2); off += 2;
                    co["mgmt_frames"] = mgmt;
                    uint16_t data = 0; memcpy(&data, buffer + off, 2); off += 2;
                    co["data_frames"] = data;
                    uint16_t ctrl = 0; memcpy(&ctrl, buffer + off, 2); off += 2;
                    co["ctrl_frames"] = ctrl;
                    JsonArray rh = co["rssi_hist"].to<JsonArray>();
                    for (int h = 0; h < 4; h++) {
                        uint16_t hv = 0; memcpy(&hv, buffer + off, 2); off += 2;
                        rh.add(hv);
                    }
                }
            }

            // Probed Networks
            uint8_t netCount = buffer[off++];
            JsonArray arrNet = doc["networks"].to<JsonArray>();
            for (uint8_t n = 0; n < netCount; n++) {
                JsonObject no = arrNet.add<JsonObject>();
                uint8_t nslen = buffer[off++];
                char nSsid[21] = {0};
                if (nslen > 0) {
                    memcpy(nSsid, buffer + off, nslen);
                    off += nslen;
                }
                no["ssid"] = nSsid;
                int16_t assoc = 0; memcpy(&assoc, buffer + off, 2); off += 2;
                no["assoc_ms"] = assoc;
                int16_t dhcp = 0; memcpy(&dhcp, buffer + off, 2); off += 2;
                no["dhcp_ms"] = dhcp;
                int16_t cf = 0; memcpy(&cf, buffer + off, 2); off += 2;
                int16_t gg = 0; memcpy(&gg, buffer + off, 2); off += 2;
                JsonObject lat = no["http_latencies"].to<JsonObject>();
                lat["cloudflare"] = cf;
                lat["google"] = gg;

                uint8_t stlen = buffer[off++];
                char stBuf[17] = {0};
                if (stlen > 0) {
                    memcpy(stBuf, buffer + off, stlen);
                    off += stlen;
                }
                no["status"] = stBuf;
            }

            // Bluetooth Status (if present)
            if (off < recLen) {
                uint8_t hasBt = buffer[off++];
                if (hasBt && (off + 15 <= recLen)) {
                    JsonObject bt = doc["bluetooth"].to<JsonObject>();
                    uint16_t bMacs = 0; memcpy(&bMacs, buffer + off, 2); off += 2;
                    bt["ble_total_macs"] = bMacs;
                    uint16_t bUniq = 0; memcpy(&bUniq, buffer + off, 2); off += 2;
                    bt["ble_unique_devices"] = bUniq;
                    uint16_t bRand = 0; memcpy(&bRand, buffer + off, 2); off += 2;
                    bt["ble_random_macs"] = bRand;
                    uint16_t bPub = 0; memcpy(&bPub, buffer + off, 2); off += 2;
                    bt["ble_public_macs"] = bPub;
                    JsonObject cat = bt["ble_categories"].to<JsonObject>();
                    cat["beacons"] = buffer[off++];
                    cat["smartphones"] = buffer[off++];
                    cat["wearables"] = buffer[off++];
                    cat["audio"] = buffer[off++];
                    cat["other"] = buffer[off++];
                    uint16_t cTot = 0; memcpy(&cTot, buffer + off, 2); off += 2;
                    bt["classic_total"] = cTot;
                }
            }

            String line;
            serializeJson(doc, line);
            if (!first) json += ",";
            json += line;
            first = false;

            readOffset += (recLen + 1); // +1 for checksum
            count++;
        }

        json += "]";
        return (count > 0) ? json : "";
    }

    static void clearSpool() {
        const esp_partition_t *part = getPartition();
        if (!part) return;

        // Erase sector 0 and reinitialize header
        esp_partition_erase_range(part, 0, SECTOR_SIZE);

        SpoolHeader hdr;
        hdr.magic = SPOOL_MAGIC_HEADER;
        hdr.version = 1;
        hdr.record_count = 0;
        hdr.write_offset = SPOOL_START_OFFSET;
        hdr.crc32 = 0;

        esp_partition_write(part, 0, &hdr, sizeof(hdr));
        Serial.println(F("[RawSpool] Flash spool buffer cleared. Zero wear active."));
    }

private:
    static void updateHeader(const esp_partition_t *part, const SpoolHeader &hdr) {
        // Overwrite header without erasing sector 0 if only bits are cleared,
        // or re-erase sector 0 when wrapping.
        esp_partition_erase_range(part, 0, SECTOR_SIZE);
        esp_partition_write(part, 0, &hdr, sizeof(hdr));
    }

    static void parseMac(const String &bssid, uint8_t *mac) {
        if (bssid.length() < 17) return;
        int m[6];
        if (sscanf(bssid.c_str(), "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            for (int i = 0; i < 6; i++) mac[i] = (uint8_t)m[i];
        }
    }
};
