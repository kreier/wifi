#pragma once

#include <Arduino.h>

// Standard ESP32 BOOT button (Active LOW with pullup)
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(TARGET_C3_SUPERMINI) || defined(CONFIG_IDF_TARGET_ESP32C5) || defined(TARGET_ESP32C5)
#define BOOT_BUTTON_PIN 9
#else
#define BOOT_BUTTON_PIN 0
#endif

// Hardware board preset types
enum BoardType : uint8_t {
    BOARD_T18 = 0,
    BOARD_T8 = 1,
    BOARD_DEVKIT = 2,
    BOARD_CUSTOM = 3,
    BOARD_C3_SUPERMINI = 4,
    BOARD_ESP32S3 = 5,
    BOARD_ESP32C5 = 6
};

struct HardwareProfile {
    uint8_t board_type;      // BoardType enum
    char board_name[32];     // e.g. "T18", "T8", "DevKitV1", "Custom", "C3SuperMini", "ESP32S3", "ESP32C5"
    int8_t status_led_pin;   // GPIO pin for status LED (-1 if none)
    int8_t rgb_led_pin;      // GPIO pin for RGB LED (-1 if none)
    int8_t battery_pin;      // ADC GPIO pin for battery reading (-1 if none)
    float battery_divider;   // LiPo divider ratio (0.5 for T8/T18, 0.33 for C5, etc.)
    bool has_5ghz;           // 5 GHz band support flag (ESP32-C5)
    bool has_wifi6;          // Wi-Fi 6 support flag (ESP32-C5/C6)
    bool led_active_low;     // Active LOW LED (ESP32-C3 Super Mini GPIO 8)

    HardwareProfile() {
#if defined(CONFIG_IDF_TARGET_ESP32C5) || defined(TARGET_ESP32C5)
        setPreset(BOARD_ESP32C5);
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(TARGET_ESP32S3)
        setPreset(BOARD_ESP32S3);
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(TARGET_C3_SUPERMINI)
        setPreset(BOARD_C3_SUPERMINI);
#else
        setPreset(BOARD_T18);
#endif
    }

    void setPreset(BoardType type) {
        board_type = type;
        rgb_led_pin = -1;
        has_5ghz = false;
        has_wifi6 = false;
        led_active_low = false;

        switch (type) {
            case BOARD_T18:
                strncpy(board_name, "T18", sizeof(board_name) - 1);
                status_led_pin = 5;
                battery_pin = 35;
                battery_divider = 0.50f; // 100k/100k
                break;
            case BOARD_T8:
                strncpy(board_name, "T8", sizeof(board_name) - 1);
                status_led_pin = 21;
                battery_pin = 35;
                battery_divider = 0.50f; // 100k/100k
                break;
            case BOARD_DEVKIT:
                strncpy(board_name, "DevKitV1", sizeof(board_name) - 1);
                status_led_pin = 2;
                battery_pin = -1;
                battery_divider = 0.50f;
                break;
            case BOARD_C3_SUPERMINI:
                strncpy(board_name, "C3SuperMini", sizeof(board_name) - 1);
                status_led_pin = 8;
                battery_pin = -1;
                battery_divider = 0.50f;
                led_active_low = true;
                break;
            case BOARD_ESP32S3:
                strncpy(board_name, "ESP32S3", sizeof(board_name) - 1);
                status_led_pin = 2;
                battery_pin = -1;
                battery_divider = 0.50f;
                has_5ghz = false;
                has_wifi6 = false;
                led_active_low = false;
                break;
            case BOARD_ESP32C5:
                strncpy(board_name, "ESP32C5", sizeof(board_name) - 1);
                status_led_pin = 8;
                battery_pin = -1;
                battery_divider = 0.33f;
                has_5ghz = true;
                has_wifi6 = true;
                led_active_low = false;
                break;
            case BOARD_CUSTOM:
            default:
                strncpy(board_name, "Custom", sizeof(board_name) - 1);
                status_led_pin = 5;
                battery_pin = -1;
                battery_divider = 0.50f;
                break;
        }
    }
};

class BatteryReader {
public:
    static uint16_t readBatteryMilliVolts(const HardwareProfile &hw) {
        if (hw.battery_pin < 0) return 0;

        pinMode(hw.battery_pin, INPUT);
        delay(5);

        // Average 5 samples for ADC stability
        uint32_t sumAdcMv = 0;
        for (int i = 0; i < 5; i++) {
            sumAdcMv += analogReadMilliVolts(hw.battery_pin);
            delay(2);
        }
        float avgAdcMv = (float)sumAdcMv / 5.0f;

        if (hw.battery_divider <= 0.01f) return (uint16_t)avgAdcMv;

        // Battery voltage = ADC reading / divider ratio (e.g. avgAdcMv / 0.5 = avgAdcMv * 2.0)
        float batMv = avgAdcMv / hw.battery_divider;
        if (batMv < 0.0f) batMv = 0.0f;
        if (batMv > 65535.0f) batMv = 65535.0f;

        return (uint16_t)batMv;
    }
};
