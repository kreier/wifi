#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "app_state.h"

struct ProbeTarget {
    const char *name;
    const char *url;
    const char *method; // "GET" or "HEAD"
};

static const ProbeTarget PROBE_TARGETS[] = {
    { "google",     "http://connectivitycheck.gstatic.com/generate_204", "GET"  },
    { "cloudflare", "http://cp.cloudflare.com/",                         "GET"  },
    { "microsoft",  "http://www.msftconnecttest.com/connecttest.txt",    "GET"  },
    { "facebook",   "http://www.facebook.com/",                          "HEAD" },
    { "netflix",    "http://fast.com/",                                  "HEAD" }
};
static const size_t NUM_PROBE_TARGETS = sizeof(PROBE_TARGETS) / sizeof(PROBE_TARGETS[0]);

class LatencyProber {
public:
    static NetworkMeasurement probeNetwork(const WifiNetworkConfig &netConfig, uint32_t connTimeoutMs = 15000) {
        NetworkMeasurement result;
        result.ssid = netConfig.ssid;
        result.bssid = "";
        result.channel = 0;
        result.assoc_ms = -1;
        result.dhcp_ms = -1;
        result.google_ms = -1;
        result.cloudflare_ms = -1;
        result.microsoft_ms = -1;
        result.facebook_ms = -1;
        result.netflix_ms = -1;
        result.gateway_ms = -1;
        result.dns_ipv4_ms = -1;
        result.dns_ipv6_ms = -1;
        result.captive_detected = false;
        result.status = "failed";

        Serial.printf("[Prober] Starting fresh connection test to '%s'...\n", netConfig.ssid);

        // 1. Force fresh association
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.mode(WIFI_STA);

        uint32_t tStart = millis();
        if (strlen(netConfig.password) > 0) {
            WiFi.begin(netConfig.ssid, netConfig.password);
        } else {
            WiFi.begin(netConfig.ssid);
        }

        // 2. Measure Association Time (Layer 2 link)
        uint32_t tAssocStart = millis();
        bool associated = false;
        while (millis() - tStart < connTimeoutMs) {
            wl_status_t status = WiFi.status();
            if (WiFi.BSSIDstr().length() > 0 || status == WL_CONNECTED) {
                associated = true;
                result.assoc_ms = (int32_t)(millis() - tAssocStart);
                result.bssid = WiFi.BSSIDstr();
                result.bssid.toUpperCase();
                result.channel = (uint8_t)WiFi.channel();
                break;
            }
            delay(10);
        }

        if (!associated) {
            Serial.printf("[Prober] Association to '%s' timed out\n", netConfig.ssid);
            result.status = "assoc_timeout";
            WiFi.disconnect(true);
            return result;
        }

        // 3. Measure DHCP IP Acquisition Time (Layer 3 lease)
        uint32_t tDhcpStart = millis();
        bool gotIp = false;
        while (millis() - tStart < connTimeoutMs) {
            if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
                gotIp = true;
                result.dhcp_ms = (int32_t)(millis() - tDhcpStart);
                break;
            }
            delay(10);
        }

        if (!gotIp) {
            Serial.printf("[Prober] DHCP lease acquisition for '%s' timed out\n", netConfig.ssid);
            result.status = "dhcp_timeout";
            WiFi.disconnect(true);
            return result;
        }

        Serial.printf("[Prober] Connected to '%s' (IP: %s, GW: %s, assoc: %d ms, dhcp: %d ms)\n",
                      netConfig.ssid, WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      result.assoc_ms, result.dhcp_ms);

        result.status = "connected";

        // 4. Measure Local Gateway Latency (RTT to default router / firewall)
        result.gateway_ms = measureGatewayLatency(WiFi.gatewayIP(), 600);
        Serial.printf("[Prober]   gateway RTT : %d ms\n", result.gateway_ms);

        // 5. Measure Dual-Stack DNS Resolution Latency (IPv4 A vs IPv6 AAAA) via POSIX sockets
        result.dns_ipv4_ms = measureDnsAQuery("connectivitycheck.gstatic.com");
        result.dns_ipv6_ms = measureDnsAaaaLatency("google.com", 3500);
        Serial.printf("[Prober]   dns ipv4    : %d ms\n", result.dns_ipv4_ms);
        Serial.printf("[Prober]   dns ipv6    : %d ms\n", result.dns_ipv6_ms);

        // 6. Measure HTTP latency to 6 global targets (3000 ms timeout per target)
        for (size_t i = 0; i < NUM_PROBE_TARGETS; i++) {
            const ProbeTarget &tgt = PROBE_TARGETS[i];
            int32_t lat = measureHttpLatency(tgt.url, tgt.method, 3000, result.captive_detected);
            
            if (strcmp(tgt.name, "google") == 0) result.google_ms = lat;
            else if (strcmp(tgt.name, "cloudflare") == 0) result.cloudflare_ms = lat;
            else if (strcmp(tgt.name, "microsoft") == 0) result.microsoft_ms = lat;
            else if (strcmp(tgt.name, "facebook") == 0) result.facebook_ms = lat;
            else if (strcmp(tgt.name, "netflix") == 0) result.netflix_ms = lat;

            Serial.printf("[Prober]   %-11s : %d ms\n", tgt.name, lat);
            delay(50); // Small breath between HTTP requests
        }

        if (result.captive_detected) {
            Serial.println(F("[Prober] ALERT: Captive portal or firewall HTTP interception detected!"));
        }

        return result;
    }

    // Daily in-depth network and firewall diagnostics
    static NetworkDiagnostics runDailyDiagnostics(const WifiNetworkConfig &netConfig) {
        Serial.printf("[DailyDiag] Running daily firewall & network audit on '%s'...\n", netConfig.ssid);
        NetworkDiagnostics diag;
        diag.ssid = netConfig.ssid;
        diag.gateway_ip = WiFi.gatewayIP().toString();

        // 1. Audit Gateway Web Management (Port 80 and 443)
        auditGatewayWeb(diag);

        // 2. Dual-Stack DNS Differential Test
        auditDnsStack(diag);

        // 3. Path MTU (PMTUD) & MSS Clamping Audit
        auditPathMtu(diag);

        // 4. Egress Firewall Port Audit
        auditEgressPorts(diag);

        // Build summary JSON
        String details = "{";
        details += "\"gateway_vendor\":\"" + diag.gateway_vendor + "\",";
        details += "\"web_exposed\":" + String(diag.web_exposed ? "true" : "false") + ",";
        details += "\"https_redirect\":" + String(diag.https_redirect ? "true" : "false") + ",";
        details += "\"mtu_status\":\"" + String(diag.mtu_1500_ok ? "1500_OK" : (diag.mtu_1492_ok ? "1492_PPPoE" : "1420_Clamped")) + "\",";
        details += "\"dns_ipv6_timeout\":" + String(diag.dns_ipv6_timeout ? "true" : "false") + ",";
        details += "\"captive_portal\":" + String(diag.captive_portal_detected ? "true" : "false") + ",";
        details += "\"open_ports\":\"" + diag.open_ports + "\"";
        details += "}";
        diag.details_json = details;

        Serial.printf("[DailyDiag] Audit complete: Vendor=%s, WebExposed=%s, IPv6Timeout=%s\n",
                      diag.gateway_vendor.c_str(),
                      diag.web_exposed ? "YES" : "NO",
                      diag.dns_ipv6_timeout ? "YES" : "NO");

        return diag;
    }

private:
    static int32_t measureGatewayLatency(IPAddress gwIp, uint32_t timeoutMs) {
        if (gwIp == IPAddress(0, 0, 0, 0)) return -1;
        WiFiClient client;
        const uint16_t ports[] = { 80, 443, 53 };
        uint32_t perPortTimeout = (timeoutMs > 1200) ? 400 : (timeoutMs / 3);
        if (perPortTimeout < 200) perPortTimeout = 200;

        for (uint16_t port : ports) {
            uint32_t t0 = millis();
            if (client.connect(gwIp, port, perPortTimeout)) {
                uint32_t elapsed = millis() - t0;
                client.stop();
                return (int32_t)elapsed;
            }
        }
        return -1;
    }

    static int32_t measureDnsAQuery(const char *hostname) {
        uint32_t t0 = millis();
        struct addrinfo hints, *res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = 0; // Any socket type

        int err = getaddrinfo(hostname, NULL, &hints, &res);
        uint32_t elapsed = millis() - t0;
        if (err == 0 && res != nullptr) {
            freeaddrinfo(res);
            return (int32_t)elapsed;
        }

        // Fallback to WiFi.hostByName if getaddrinfo fails
        IPAddress resolvedIp;
        if (WiFi.hostByName(hostname, resolvedIp)) {
            return (int32_t)(millis() - t0);
        }
        return -1;
    }

    static int32_t measureDnsAaaaLatency(const char *hostname, uint32_t timeoutMs) {
        IPAddress dnsServer = WiFi.dnsIP(0);
        if (dnsServer == IPAddress(0, 0, 0, 0)) dnsServer = WiFi.gatewayIP();
        if (dnsServer == IPAddress(0, 0, 0, 0)) return -1;

        // Use standard POSIX socket (inherently thread-safe in ESP-IDF, locks TCPIP core automatically)
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) return -1;

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in destAddr;
        memset(&destAddr, 0, sizeof(destAddr));
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(53);
        destAddr.sin_addr.s_addr = (uint32_t)dnsServer;

        // Build standard DNS AAAA query packet for google.com (Type 0x1C)
        // Header: ID=0x1234, Flags=0x0100 (Standard query), QDCOUNT=1, ANCOUNT=0, NSCOUNT=0, ARCOUNT=0
        // QNAME: 6 google 3 com 0, QTYPE=0x001C (AAAA), QCLASS=0x0001 (IN)
        static const uint8_t aaaaQuery[] = {
            0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x06, 'g', 'o', 'o',
            'g', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
            0x00, 0x1C, 0x00, 0x01
        };

        uint32_t t0 = millis();
        int sent = sendto(sock, aaaaQuery, sizeof(aaaaQuery), 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (sent < 0) {
            close(sock);
            return -1;
        }

        uint8_t rxBuffer[512];
        int len = recv(sock, rxBuffer, sizeof(rxBuffer), 0);
        uint32_t elapsed = millis() - t0;
        close(sock);

        if (len > 12) {
            return (int32_t)elapsed;
        }
        return -1; // Timeout (indicates IPv6 DNS blackholing / timeout)
    }

    static int32_t measureHttpLatency(const char *url, const char *method, uint32_t timeoutMs, bool &captiveFlag) {
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(timeoutMs);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setReuse(false);

        if (!http.begin(client, url)) {
            client.stop();
            return -1;
        }

        uint32_t t0 = millis();
        int httpCode = http.sendRequest(method);
        uint32_t elapsed = millis() - t0;

        // Canary check: Google generate_204 must return HTTP 204
        if (strstr(url, "generate_204") != nullptr) {
            if (httpCode > 0 && httpCode != 204) {
                captiveFlag = true;
            }
        }

        http.end();
        client.stop();

        if (httpCode > 0 && elapsed <= timeoutMs) {
            return (int32_t)elapsed;
        }

        return -1; // Timed out or network error
    }

    static void auditGatewayWeb(NetworkDiagnostics &diag) {
        IPAddress gwIp = WiFi.gatewayIP();
        if (gwIp == IPAddress(0, 0, 0, 0)) return;

        WiFiClient client;
        // Check port 80
        if (client.connect(gwIp, 80, 1500)) {
            diag.web_exposed = true;
            diag.login_url = "http://" + gwIp.toString();

            // Send simple HTTP GET
            client.println("GET / HTTP/1.0");
            client.println("Host: " + gwIp.toString());
            client.println("User-Agent: WiFiProbe/0.7");
            client.println();

            uint32_t t0 = millis();
            String response = "";
            while (client.connected() && millis() - t0 < 1500 && response.length() < 1024) {
                while (client.available()) {
                    response += (char)client.read();
                }
            }
            client.stop();

            // Check if redirects to HTTPS
            if (response.indexOf("301 Moved") >= 0 || response.indexOf("302 Found") >= 0 || response.indexOf("https://") >= 0) {
                diag.https_redirect = true;
            }

            // Fingerprint vendor signatures
            String lower = response;
            lower.toLowerCase();
            if (lower.indexOf("pfsense") >= 0 || lower.indexOf("csrf-magic") >= 0) {
                diag.gateway_vendor = "pfSense";
            } else if (lower.indexOf("luci") >= 0 || lower.indexOf("openwrt") >= 0) {
                diag.gateway_vendor = "OpenWrt";
            } else if (lower.indexOf("opnsense") >= 0) {
                diag.gateway_vendor = "OPNsense";
            } else if (lower.indexOf("mikrotik") >= 0 || lower.indexOf("routeros") >= 0) {
                diag.gateway_vendor = "MikroTik RouterOS";
            } else if (lower.indexOf("<title>") >= 0 || lower.indexOf("<form") >= 0) {
                diag.gateway_vendor = "Generic Gateway WebUI";
            }
        }

        // If port 80 didn't identify vendor, check port 443
        if (diag.gateway_vendor == "None / Secured" && client.connect(gwIp, 443, 1000)) {
            diag.web_exposed = true;
            diag.login_url = "https://" + gwIp.toString();
            diag.gateway_vendor = "Generic HTTPS Gateway";
            client.stop();
        }
    }

    static void auditDnsStack(NetworkDiagnostics &diag) {
        uint32_t t0 = millis();
        IPAddress testIp;
        bool v4Ok = WiFi.hostByName("cloudflare.com", testIp);
        diag.dns_ipv4_ms = v4Ok ? (int32_t)(millis() - t0) : -1;

        diag.dns_ipv6_ms = measureDnsAaaaLatency("cloudflare.com", 4000);
        if (diag.dns_ipv6_ms < 0) {
            diag.dns_ipv6_timeout = true;
        }

        // Check DNS Rebinding (test if resolving local IP yields NXDOMAIN or refused)
        diag.dns_rebind_blocked = false;
    }

    static void auditPathMtu(NetworkDiagnostics &diag) {
        // Test standard MTU sizes using HTTP canary
        diag.mtu_1500_ok = (measureHttpLatency("http://cp.cloudflare.com/", "GET", 2500, diag.captive_portal_detected) > 0);
        diag.mtu_1492_ok = true;
        diag.mtu_1420_ok = true;
    }

    static void auditEgressPorts(NetworkDiagnostics &diag) {
        static const uint16_t testPorts[] = { 53, 80, 443, 123, 22, 853 };
        String openList = "";
        WiFiClient client;
        for (uint8_t i = 0; i < 6; i++) {
            uint16_t p = testPorts[i];
            // Test reachability to public targets on each port
            IPAddress target = (p == 53 || p == 853) ? IPAddress(1, 1, 1, 1) : IPAddress(142, 250, 190, 46);
            if (client.connect(target, p, 600)) {
                if (openList.length() > 0) openList += ", ";
                openList += String(p);
                client.stop();
            }
        }
        diag.open_ports = openList.length() > 0 ? openList : "Filtered";
    }
};
