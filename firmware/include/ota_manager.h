#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"

class OtaManager {
public:
    static bool checkForUpdate(const String &workerUrl, const String &stationId,
                              const String &currentVersion, const HardwareProfile &hw,
                              bool *pIsCanary = nullptr, AppConfigManager *configMgr = nullptr) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[OTA] Cannot check for updates: WiFi not connected"));
            return false;
        }

        bool isCanaryLocal = pIsCanary ? *pIsCanary : false;

        String checkEndpoint = workerUrl;
        if (checkEndpoint.endsWith("/")) checkEndpoint.remove(checkEndpoint.length() - 1);
        checkEndpoint += "/api/ota?station_id=" + stationId +
                         "&version=" + currentVersion +
                         "&board=" + String(hw.board_name) +
                         "&canary=" + String(isCanaryLocal ? "1" : "0");

        Serial.printf("[OTA] Checking for firmware updates at: %s\n", checkEndpoint.c_str());

        WiFiClientSecure client;
        client.setInsecure(); // Cloudflare TLS validation

        HTTPClient http;
        http.setTimeout(15000);
        http.setReuse(false);

        if (!http.begin(client, checkEndpoint)) {
            Serial.println(F("[OTA] Failed to initialize HTTPS connection for update check"));
            return false;
        }

        int httpCode = http.GET();
        if (httpCode < 0) {
            Serial.printf("[OTA] Initial connection failed (%d). Retrying HTTPS check in 1000 ms...\n", httpCode);
            delay(1000);
            httpCode = http.GET();
        }
        Serial.printf("[OTA] Server check response: HTTP %d\n", httpCode);

        if (httpCode == 200) {
            String payload = http.getString();
            http.end();

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (err) {
                Serial.printf("[OTA] Failed to parse OTA response: %s\n", err.c_str());
                return false;
            }

            // Sync canary track setting from server
            if (doc.containsKey("is_canary")) {
                bool serverCanary = doc["is_canary"].as<bool>();
                if (pIsCanary && *pIsCanary != serverCanary) {
                    Serial.printf("[OTA] Updating canary track setting from server: %s -> %s\n",
                                  *pIsCanary ? "true" : "false", serverCanary ? "true" : "false");
                    *pIsCanary = serverCanary;
                    if (configMgr) {
                        configMgr->setCanaryTrack(serverCanary);
                    }
                }
            }

            bool updateAvailable = doc["update_available"] | false;
            const char *targetVersion = doc["target_version"] | "";
            const char *binUrl = doc["url"] | "";
            const char *track = doc["track"] | "";
            bool allowDowngrade = doc["allow_downgrade"] | false;

            if (updateAvailable && strlen(binUrl) > 0) {
                bool isCanaryBuild = (strcmp(track, "canary") == 0) ||
                                     (strstr(targetVersion, "canary") != nullptr) ||
                                     (strstr(targetVersion, "beta") != nullptr);
                bool canReceiveCanary = pIsCanary ? *pIsCanary : false;

                if (isCanaryBuild && !canReceiveCanary) {
                    Serial.printf("[OTA] REJECTED: Firmware '%s' is on canary track, but device is not enrolled in canary.\n", targetVersion);
                    return false;
                }

                // Safeguard: Never accept an automatic downgrade or equal version
                if (!allowDowngrade && compareSemVer(String(targetVersion), currentVersion) <= 0) {
                    Serial.printf("[OTA] Ignoring downgrade/same version offer (Target: %s <= Current: %s)\n",
                                  targetVersion, currentVersion.c_str());
                    return false;
                }

                Serial.println(F("\n=========================================="));
                Serial.printf(" [OTA] UPDATE FOUND! Target: %s (Track: %s)\n", targetVersion, isCanaryBuild ? "CANARY" : "STABLE");
                Serial.printf(" [OTA] Binary URL: %s\n", binUrl);
                Serial.println(F("==========================================\n"));

                return performOtaUpdate(binUrl);
            } else {
                Serial.printf("[OTA] Up to date (Running v%s)\n", currentVersion.c_str());
            }
        } else if (httpCode == 304) {
            Serial.println(F("[OTA] Firmware is up to date (HTTP 304 Not Modified)."));
            http.end();
        } else {
            Serial.printf("[OTA] Check failed with HTTP status %d\n", httpCode);
            http.end();
        }

        return false;
    }

    static bool performOtaUpdate(const String &binaryUrl) {
        WiFiClientSecure otaClient;
        otaClient.setInsecure();

        // Automatically reboot on successful flash
        httpUpdate.rebootOnUpdate(true);

        Serial.println(F("[OTA] Streaming new firmware directly to inactive OTA flash partition..."));
        t_httpUpdate_return ret = httpUpdate.update(otaClient, binaryUrl);

        switch (ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("[OTA] ERROR: Update failed (%d): %s\n",
                              httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                return false;
            case HTTP_UPDATE_NO_UPDATES:
                Serial.println(F("[OTA] No updates available on server."));
                return false;
            case HTTP_UPDATE_OK:
                Serial.println(F("[OTA] SUCCESS! Firmware verified. Device is rebooting into new version..."));
                return true;
        }
        return false;
    }

    static int compareSemVer(const String &v1, const String &v2) {
        int maj1 = 0, min1 = 0, pat1 = 0;
        int maj2 = 0, min2 = 0, pat2 = 0;
        const char *s1 = v1.c_str();
        const char *s2 = v2.c_str();
        if (s1[0] == 'v' || s1[0] == 'V') s1++;
        if (s2[0] == 'v' || s2[0] == 'V') s2++;
        sscanf(s1, "%d.%d.%d", &maj1, &min1, &pat1);
        sscanf(s2, "%d.%d.%d", &maj2, &min2, &pat2);
        if (maj1 != maj2) return maj1 - maj2;
        if (min1 != min2) return min1 - min2;
        if (pat1 != pat2) return pat1 - pat2;
        bool pre1 = (strchr(s1, '-') != nullptr);
        bool pre2 = (strchr(s2, '-') != nullptr);
        if (pre1 && !pre2) return -1;
        if (!pre1 && pre2) return 1;
        return 0;
    }
};
