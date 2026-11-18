# kreier/wifi

[![Latest Release](https://img.shields.io/github/v/release/kreier/wifi?color=blue&label=version)](https://github.com/kreier/wifi/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](https://opensource.org/licenses/Apache-2.0)
[![Cloudflare Worker](https://img.shields.io/badge/Cloudflare-wifi.kreier.workers.dev-F38020?logo=cloudflare)](https://wifi.kreier.workers.dev/)
[![GitHub Pages](https://img.shields.io/badge/GitHub%20Pages-kreier.github.io%2Fwifi-22c55e?logo=github)](https://kreier.github.io/wifi/)

> Autonomous Wi-Fi Spectrum & Latency Telemetry Fleet Node for ESP32 with Cloudflare Worker, D1 SQLite Database, and GitHub Pages.

**Live Telemetry Dashboards & Web Installer**:
- 🌐 **Cloudflare Edge Dashboard**: [https://wifi.kreier.workers.dev/](https://wifi.kreier.workers.dev/)
- 📄 **GitHub Pages Mirror**: [https://kreier.github.io/wifi/](https://kreier.github.io/wifi/)
- ⚡ **In-Browser ESP32 Web Flasher**: [https://wifi.kreier.workers.dev/?flash=1](https://wifi.kreier.workers.dev/?flash=1) or [https://kreier.github.io/wifi/?flash=1](https://kreier.github.io/wifi/?flash=1)

**Direct Station Access**:
Both the Cloudflare Worker and GitHub Pages support deep-linking directly to specific monitoring stations:
- View Station **9E1-9**: [https://wifi.kreier.workers.dev/9E1-9](https://wifi.kreier.workers.dev/9E1-9) or [https://kreier.github.io/wifi/9E1-9](https://kreier.github.io/wifi/9E1-9)

---

## Key Architectural Features

The `kreier/wifi` platform is built on an edge-native, zero-trust, and power-optimized architecture designed for autonomous continuous Wi-Fi fleet telemetry:

- **Serverless Edge Storage & Analytics ([ADR 0001: Cloudflare D1 Storage](docs/adr/0001-d1-over-git-storage.md))**:
  All telemetry cycles, spectrum scans, channel congestion metrics, and daily rollups are stored in **Cloudflare D1** (serverless SQLite at edge). Automated daily cron triggers aggregate 24-hour summaries and enforce 30-day raw data retention.
- **Zero-Trust Device Provisioning ([ADR 0002: Per-Device Token Provisioning](docs/adr/0002-per-device-token-provisioning.md))**:
  Devices are commissioned via an on-device captive portal using a one-time bootstrap phrase (`MASTER_PHRASE`). Upon the first successful connection, a unique cryptographically-random Bearer token is negotiated with the backend and stored in hardware NVS; the master phrase is permanently erased from NVS flash and RAM.
- **Dual-Compatible Firmware Layout ([ADR 0003: Single-Source Dual Build](docs/adr/0003-dual-build-layout.md))**:
  Maintains a unified modular codebase in `firmware/include/` that compiles seamlessly in both **PlatformIO** (automated CI/CD pipelines, multi-board target flags) and **Arduino IDE** (single-click desktop flashing without external CLI dependencies).
- **2.4 GHz Promiscuous RF Contention Profiling**:
  Measures all 13 channels every 30 minutes with 2-second dwell times using ESP-IDF promiscuous mode. Classifies frames (Management, Data, Control), records 4-bracket RSSI histograms, counts unique transmitter MACs, and estimates PHY-modeled airtime percentage.
- **2.4 GHz Bluetooth Classic & BLE Ingestion ([ADR 0005: Bluetooth Spectrum & Payload De-Randomization](docs/adr/0005-bluetooth-ble-spectrum-and-derandomization.md))**:
  Dual Bluetooth Low Energy (BLE) active scan and Bluetooth Classic (BR/EDR) inquiry integrated into the 10-minute cycle. Employs advertising payload fingerprinting to defeat MAC address randomization, clustering anonymous RPAs by vendor, service UUIDs, and RSSI zones while uniquely fingerprinting Apple iBeacons, Google Eddystone, AirTags/Find My, and named peripherals. Decodes Classic BT Class of Device (COD) descriptors and visualizes 24h presence trends.
- **10-Minute Spectrum Scan Variance Tracking**:
  Monitors the duration of full-spectrum scans in milliseconds on every 10-minute cycle, correlating scan latency with RF contention.
- **Local Network & Dual-Stack Diagnostics**:
  Measures local gateway TCP SYN round-trip time, performs IPv4 vs. IPv6 DNS resolution benchmarking, detects captive portal HTTP canary hijacking, and executes automated daily gateway firewall audits.
- **Automated Fleet OTA & Battery Telemetry ([ADR 0004: Fleet OTA & Battery History](docs/adr/0004-fleet-ota-and-battery-monitoring.md))**:
  Automated edge OTA dispatch with zero-redirect streaming proxies from GitHub Releases and Cloudflare R2. Time-series battery voltage monitoring (Migration 0008) on ADC pin IO35 with discharge trend analysis and remaining runtime modeling.
- **Offline Resilience via Raw SPI Flash Partition Buffer**:
  When Wi-Fi connectivity is unavailable, cycles are binary-buffered into a dedicated 192 KB raw flash circular spool, with in-memory zero-flash-wear fast path during normal connectivity.
- **Dual Web Dashboards**:
  Edge-hosted server-rendered dashboard on Cloudflare Worker (`wifi.kreier.workers.dev`) coupled with a GitHub Pages static client mirror (`kreier.github.io/wifi`), both supporting client-side station deep-linking and theme switching.
- **In-Browser Web Serial Flasher**:
  Zero-install browser flasher leveraging the Web Serial API and ESP Web Tools. Enables operators to flash compiled universal firmware and partition tables directly to ESP32 nodes with real-time progress and live serial console logs from Chrome, Edge, Brave, or Opera without installing local toolchains or Python.

---

## Power-Aware Measurement Cadence Matrix

To maximize battery runtime on single-cell 18650 LiPo cells while delivering high-resolution spectrum telemetry, the firmware dynamically selects measurement cadences based on voltage sensed on **IO35**:

| Measurement Task | Battery Powered ($\le 4.20\text{ V}$) | USB / AC Powered ($> 4.20\text{ V}$ / No Bat) | RAM Storage & Rollup Invariant |
| :--- | :---: | :---: | :--- |
| **1) Wi-Fi Spectrum Scan** | Every 10 minutes | Every 2 minutes | Peak RSSI & quality accumulated in RAM per 10m window |
| **2) BLE & Classic Bluetooth Scan** | Every 10 minutes | Every 2 minutes | Max unique BLE & Classic BT devices accumulated in RAM |
| **3) 13-Channel Airtime Utilization** | Every 30 minutes | Every 2 minutes | Max airtime % across 13 channels accumulated in RAM |
| **4) Station Speed & Latency Probes** | Every 30 minutes *(Visible APs Only)* | Every 10 minutes *(Visible APs Only)* | Gateway, DHCP, DNS, and HTTP latency probes |
| **Telemetry Upload Attempt** | Every 10 minutes *(If AP or S24 visible)* | Every 10 minutes *(If AP or S24 visible)* | Directly transmitted from RAM (Zero flash wear when online) |
| **Flash Persistence** | Offline only (30-min buffer + spool) | Offline only (30-min buffer + spool) | No flash writes when telemetry upload succeeds |

---

## Architecture Overview

```mermaid
flowchart TD
    subgraph EdgeNodes["Edge Fleet Nodes (ESP32 Microcontrollers)"]
        subgraph HardwareProfiles["Hardware Targets"]
            T18["LILYGO TTGO T18<br/>(IO5 LED, IO35 Battery Divider)"]
            T8["LILYGO T8<br/>(IO21 LED, IO35 Battery Divider)"]
            DevKit["ESP32 DevKit V1 / Custom<br/>(IO2 LED / Configurable)"]
        end

        subgraph CadenceManager["Adaptive Power Cadence & RAM Aggregation"]
            PowerSense{"Battery Sensing (IO35)<br/>Vbat <= 4.2V ?"}
            BatMode["Battery Cadence (10-min interval)<br/>• Wi-Fi Scan: Every 10m<br/>• BLE + Classic Scan: Every 10m<br/>• 13-Ch Airtime: Every 30m<br/>• Speed/Latency Probes: Every 30m"]
            PwrMode["Powered Cadence (2-min interval)<br/>• Wi-Fi Scan: Every 2m (RAM Max)<br/>• BLE + Classic Scan: Every 2m (RAM Max)<br/>• 13-Ch Airtime: Every 2m (RAM Max)<br/>• Speed/Latency Probes: Every 10m"]
            RAMBuffer["In-Memory RAM Accumulator<br/>(Max RSSI, Max Airtime %, Unique BT Devices)"]
        end

        subgraph StoragePipeline["Storage & Spooling Engine"]
            ChanHistRAM["48-Slot 24h Revolving Buffer<br/>(In-Memory Channel & BT History)"]
            FlashSpool["Offline SPI Flash Spool<br/>(Raw Circular Buffer, 190 KB)"]
            FlashHist["Flash Channel History Sectors<br/>(Preserved only when offline)"]
        end
    end

    subgraph RFEnvironment["Wireless Spectrum & Targets"]
        APScan["2.4 GHz 802.11b/g/n Spectrum<br/>(Survey APs & BSSIDs)"]
        AirtimeScan["13 Promiscuous Channels<br/>(Frame Counters & Contention %)"]
        BTScan["Bluetooth Low Energy & Classic<br/>(RPA De-randomization & OUI / COD)"]
        SpeedProbe["Target Network Gateways<br/>(DHCP, DNS IPv4/v6, HTTP Latencies)"]
        BackupAP["Backup Uplink Hotspot<br/>('S24' / Emergency Access Point)"]
    end

    subgraph CloudflareEdge["Cloudflare Serverless Edge Infrastructure"]
        WorkerRouter["Cloudflare Worker Router<br/>(https://wifi.kreier.workers.dev)"]
        AuthIngest["Bearer Token & Zero-Trust Validation<br/>(/data, /register)"]
        OTAService["OTA Firmware Streaming Proxy<br/>(/api/ota, /firmware/*)"]
        CronJob["Daily Maintenance Cron (00:15 UTC)<br/>• 48-Interval 30-min Rollups<br/>• Change-Only Diagnostics History<br/>• 30-Day Purge & Rotation"]

        subgraph D1Database["Cloudflare D1 SQLite Database (wifi-mon-db)"]
            T_Meas["measurements & daily_summaries"]
            T_30Min["daily_30min_summaries (48 slots/day)"]
            T_Chan["station_channel_utilization"]
            T_BT["station_bluetooth_summary & latest"]
            T_Diag["station_network_diagnostics<br/>& diagnostics_history (Change-only)"]
            T_Bat["station_battery_history"]
        end
    end

    subgraph FrontendClients["Dual Dashboards & Management Tools"]
        WorkerUI["Server-Rendered Dashboard<br/>(Cloudflare Worker SSR)"]
        PagesUI["Static Dashboard Mirror<br/>(GitHub Pages: kreier.github.io/wifi)"]
        WebFlasher["Web Serial Browser Flasher<br/>(ESP Web Tools Zero-Install)"]
    end

    %% Edge node data flows
    HardwareProfiles --> PowerSense
    PowerSense -->|Battery <= 4.2V| BatMode
    PowerSense -->|USB / AC > 4.2V| PwrMode
    BatMode --> RAMBuffer
    PwrMode --> RAMBuffer

    RAMBuffer --> APScan
    RAMBuffer --> AirtimeScan
    RAMBuffer --> BTScan
    RAMBuffer --> SpeedProbe

    SpeedProbe -.->|Probe only if visible| APScan

    RAMBuffer --> ChanHistRAM

    ChanHistRAM -->|Online Direct RAM Stream| WorkerRouter
    ChanHistRAM -->|Upload Failed / Offline| FlashHist
    RAMBuffer -->|Upload Failed / Offline| FlashSpool

    BackupAP -.->|Failover Uplink| WorkerRouter

    %% Worker backend flows
    WorkerRouter --> AuthIngest
    WorkerRouter --> OTAService
    AuthIngest --> D1Database
    CronJob --> D1Database

    %% Frontend client flows
    D1Database --> WorkerUI
    D1Database --> PagesUI
    OTAService --> WebFlasher
```

---

## Repository Structure

```
kreier/wifi/
├── .github/
│   └── workflows/
│       ├── build-release.yml # Builds firmware and packages release assets
│       └── deploy-pages.yml  # Deploys docs to GitHub Pages
├── docs/                     # Static GitHub Pages web files & ADRs
│   ├── index.html            # Standalone responsive web dashboard
│   ├── 404.html              # SPA client router for direct station routes (e.g. /9E1-9)
│   └── adr/                  # Architecture Decision Records
│       ├── 0001-d1-over-git-storage.md
│       ├── 0002-per-device-token-provisioning.md
│       ├── 0003-dual-build-layout.md
│       ├── 0004-fleet-ota-and-battery-monitoring.md
│       ├── 0005-bluetooth-ble-spectrum-and-derandomization.md
│       └── 0006-power-aware-cadence-and-30min-aggregation.md
├── firmware/                 # Dual-compatible firmware source
│   ├── platformio.ini        # PlatformIO targets: esp32dev unified
│   ├── src/
│   │   └── main.cpp          # PlatformIO compilation entrypoint
│   ├── wifi/
│   │   └── wifi.ino          # Arduino IDE sketch entrypoint
│   └── include/              # Shared modular firmware components
│       ├── app_config.h      # Preferences NVS config & token management
│       ├── app_main.h        # Master lifecycle & power-aware cadence manager
│       ├── app_state.h       # Shared data models and structs
│       ├── api_client.h      # HTTPS telemetry upload client
│       ├── bluetooth_scanner.h # 2.4 GHz BLE & Classic scan engine
│       ├── board_pins.h      # Hardware profile pin mappings & battery ADC
│       ├── captive_portal.h  # Soft-AP captive portal setup server
│       ├── channel_analyzer.h # Promiscuous 13-channel congestion survey
│       ├── channel_history.h # 48-slot revolving 24h 30-minute buffer
│       ├── latency_prober.h  # Station connection & speed probes
│       ├── led_controller.h  # Non-blocking LED status indicator
│       ├── ntp_sync.h        # SNTP time synchronization manager
│       ├── ota_manager.h     # Background OTA update manager
│       ├── spool_manager.h   # Raw SPI flash circular spool
│       └── wifi_scanner.h    # 2.4 GHz spectrum scan survey engine
│       ├── app_main.h        # Top-level state machine & lifecycle
│       ├── app_state.h       # Shared data models & structures
│       ├── api_client.h      # HTTPS /register and /data client
│       ├── bluetooth_scanner.h # 2.4 GHz BLE & Classic scan with payload de-randomization
│       ├── board_pins.h      # Runtime board hardware presets & BatteryReader
│       ├── captive_portal.h  # Soft-AP, captive DNS server, & setup UI
│       ├── latency_prober.h  # Association, DHCP, DNS, and HTTP latency probes
│       ├── channel_analyzer.h # Promiscuous 13-channel utilization & airtime analyzer
│       ├── led_controller.h  # Non-blocking LED status indicator
│       ├── ntp_sync.h        # SNTP synchronization engine (24h interval)
│       ├── ota_manager.h     # Background OTA update checker & binary streaming client
│       ├── spool_manager.h   # Raw flash partition binary circular buffer
│       └── wifi_scanner.h    # 802.11 b/g/n spectrum analyzer
├── worker/                   # Cloudflare Worker & D1 Backend
│   ├── wrangler.toml         # Worker bindings, D1 config, & cron schedule
│   ├── package.json          # TypeScript & Cloudflare Worker dependencies
│   ├── tsconfig.json         # TypeScript compiler configuration
│   ├── migrations/           # D1 migrations (0001–0010: telemetry, diagnostics, battery, bluetooth)
│   └── src/
│       ├── index.ts          # Request router and scheduled event handler
│       ├── types.ts          # Domain interfaces & payloads
│       ├── db.ts             # D1 database query abstraction layer
│       ├── register.ts       # POST /register endpoint handler
│       ├── telemetry.ts      # POST /data telemetry ingestion handler
│       ├── api.ts            # Public GET /api endpoints (stations, details, export)
│       ├── cron.ts           # Daily rollup aggregation & 30-day retention pruning
│       └── frontend.ts       # Responsive single-page dashboard HTML/CSS/JS
├── data/                     # Reserved for future permanent telemetry exports
│   └── .gitkeep
├── LICENSE                   # Apache 2.0 License
└── README.md
```

---

## Hardware Targets & Pinout

Designed for generic ESP32 modules (4 MB Flash, 520 KB SRAM, no external PSRAM required). Three pre-configured environments define board-specific status LED pins:

| Environment | Target Board | Status LED Pin | Default Flag |
| :--- | :--- | :---: | :--- |
| `t18` | **T18 Custom Board** | **GPIO 5** | `-D STATUS_LED_PIN=5` *(Default)* |
| `devkitv1` | **ESP32 DevKit V1** (NodeMCU-32S) | **GPIO 2** | `-D STATUS_LED_PIN=2` |
| `t8` | **T8 ESP32 Board** | **GPIO 21** | `-D STATUS_LED_PIN=21` |

- **BOOT Button**: Standard active-low button on **GPIO 0**.
  - Hold for **3 seconds** at boot or wake to force the device into Captive Portal mode.

---

## Direct Station Routing

Both dashboards support deep-linking directly to a station's real-time metrics and survey:

- **Cloudflare Edge**: `https://wifi.kreier.workers.dev/:station_id` (e.g. [`https://wifi.kreier.workers.dev/9E1-9`](https://wifi.kreier.workers.dev/9E1-9))
- **GitHub Pages**: `https://kreier.github.io/wifi/:station_id` (e.g. [`https://kreier.github.io/wifi/9E1-9`](https://kreier.github.io/wifi/9E1-9))

Switching stations dynamically updates the URL via `history.pushState` without reloading the page.

---

## Zero-Trust Token Provisioning

To prevent secret leakage from physical extraction of flash dumps:
1. Operator inputs `MASTER_PHRASE` (bootstrap secret) into the captive portal.
2. ESP32 connects to Wi-Fi and issues `POST /register`:
   ```json
   {
     "station_id": "9E1-9",
     "description": "Sky Garden",
     "bootstrap_secret": "<MASTER_PHRASE>"
   }
   ```
3. Worker validates `bootstrap_secret === env.MASTER_PHRASE`, generates a cryptographic 32-byte UUID `device_token`, upserts the station in D1, and returns:
   ```json
   { "device_token": "a4f89d32-8419-4b68-b732-23c72b22f180" }
   ```
4. ESP32 saves `device_token` to Non-Volatile Storage (`Preferences`), zeroes out `MASTER_PHRASE` in RAM, and removes it permanently from NVS Preferences.
5. All future telemetry uses `Authorization: Bearer <device_token>`.

---

## Measurement Engine (10-Minute Cadence)

Every 10 minutes, the probe executes:

1. **Promiscuous 13-Channel Congestion Survey**: 2-second dwell times per channel measuring Management, Data, and Control frames, 4-bracket RSSI histograms, unique transmitter counts, and PHY-modeled airtime percentage.
2. **2.4 GHz Spectrum Survey**: Scans all visible 2.4 GHz BSSIDs (SSID, BSSID, RSSI, Link Quality %, Encryption, Phy Mode, Channel, Band) and detects available monitored networks and backup APs.
3. **Intelligent 24-Hour NTP Sync & Early OTA Check**: Checks visible monitored networks or falls back to backup AP (`S24`) to resynchronize local time and check for firmware updates, skipping offline networks without connection delay.
4. **Battery Voltage & Bluetooth Survey**: Reads battery ADC voltage on IO35 and conducts dual BLE and Classic Bluetooth inquiry with payload de-randomization.
5. **Sequential Network Benchmarks** (Up to 5 configured SSIDs):
   - **Association Latency** ($T_{\text{assoc}}$ in ms): Layer 2 connection time.
   - **DHCP IP Acquisition** ($T_{\text{dhcp}}$ in ms): Layer 3 lease assignment time.
   - **HTTP Latency** (3000 ms timeout per target):
     - **Google**: `http://connectivitycheck.gstatic.com/generate_204`
     - **Cloudflare**: `http://cp.cloudflare.com/`
     - **Microsoft**: `http://www.msftconnecttest.com/connecttest.txt`
     - **Facebook**: `http://www.facebook.com/`
     - **Netflix**: `http://fast.com/`
6. **In-Memory-First Direct RAM Upload**: Telemetry is transmitted directly from RAM to Cloudflare Worker. If primary networks fail, automatically fails over to backup AP.
7. **Offline Binary Spool Buffer**: If all uplinks fail, records are serialized into a packed binary circular buffer in the raw flash partition (`raw_spool`, 192 KB), holding over 50+ hours of full telemetry. Once connectivity restores, backlog records flush automatically.
8. **ESP32 Light Sleep**: Subsystems and radio are deactivated; CPU enters light sleep for the remainder of the 600-second window.

---

## Cloudflare Worker & D1 Setup

### 1. Install & Authenticate
```bash
cd worker
npm install
npx wrangler login
```

### 2. Create D1 Database & Run Migrations
```bash
# Create the D1 database
npx wrangler d1 create wifi-mon-db

# Apply schema migrations
npx wrangler d1 migrations apply wifi-mon-db --remote
```

### 3. Set Master Bootstrap Secret
```bash
npx wrangler secret put MASTER_PHRASE
# Enter your secure bootstrap secret
```

### 4. Deploy Worker
```bash
npx wrangler deploy
```

---

## Firmware Build & Flash

### Option A: In-Browser Web Flasher (Zero-Install — Recommended)
Flash pre-compiled universal firmware directly to any ESP32 board over USB using your web browser:

1. **Open the Web Flasher**:
   - 🌐 [https://wifi.kreier.workers.dev/?flash=1](https://wifi.kreier.workers.dev/?flash=1) (Cloudflare Edge)
   - 📄 [https://kreier.github.io/wifi/?flash=1](https://kreier.github.io/wifi/?flash=1) (GitHub Pages Mirror)
   - Or click **⚡ Flash Device** in the top navigation header of any dashboard view.
2. **Connect your ESP32** via a USB data cable to your desktop computer.
3. Supported browsers: **Google Chrome**, **Microsoft Edge**, **Brave**, or **Opera** on desktop (requires Web Serial API over HTTPS).
4. Click **⚡ Connect ESP32 & Install Firmware** and select your board's serial port (`CP210x`, `CH340`, or `USB JTAG/serial debug unit`).
5. Choose **Install WIFI Monitor** and check **Erase device** for a fresh clean installation.
6. The installer downloads the binary image, writes all partition tables and application slots, and verifies flash in real-time. Once complete, you can open the built-in serial console or proceed to first-boot commissioning!

> **Troubleshooting Connection**: If the browser times out or cannot synchronize with the ESP32 chip, press and hold the **BOOT (GPIO 0)** button on your board while clicking Connect, then release it once erasing/flashing begins.

---

### Option B: Pre-Compiled Binary Flashing (`esptool.py`)
If you prefer command-line flashing without compiling from source, download the compiled binaries from the Web Flasher dialog or [GitHub Releases](https://github.com/kreier/wifi/releases/latest):

- `firmware.bin` (Application — offset `0x10000`)
- `partitions.bin` (Partition Table — offset `0x8000`)
- `bootloader.bin` (Second Stage Bootloader — offset `0x1000`)
- `boot_app0.bin` (OTA Data Initializer — offset `0xe000`)
- `firmware-esp32-merged.bin` (Unified all-in-one image — offset `0x0`)

**Multi-part Flash**:
```bash
esptool.py --chip esp32 --baud 921600 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

**Single Unified Binary Flash**:
```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x0 firmware-esp32-merged.bin
```

---

### Option C: PlatformIO (Source Build)
```bash
cd firmware

# Build and upload for ESP32 Universal Target
pio run -e esp32dev -t upload

# Monitor Serial Output (115200 baud)
pio device monitor -b 115200
```

---

### Option D: Arduino IDE (Source Build)
1. Open Arduino IDE 2.x.
2. Open [`firmware/wifi/wifi.ino`](firmware/wifi/wifi.ino).
3. Under **Tools > Board**, select **ESP32 Dev Module**.
4. Set **Partition Scheme** to **Minimal SPIFFS (1.9MB OTA with spiffs)** (or custom `partitions/min_spiffs.csv`).
5. Install the **ArduinoJson** library (v7.x) via the Library Manager.
6. Click **Upload**.

---

## API Reference

### `POST /register`
Registers a new station using the bootstrap secret.
- **Request Body**:
  ```json
  {
    "station_id": "9E1-9",
    "description": "Sky Garden",
    "bootstrap_secret": "<MASTER_PHRASE>"
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "success": true,
    "station_id": "9E1-9",
    "device_token": "d748f220-4f51-4e08-9df8-0d12e6973347"
  }
  ```

### `POST /data`
Submits raw telemetry cycle data.
- **Header**: `Authorization: Bearer <device_token>`
- **Request Body**: Single telemetry object or JSON array of spooled cycles.
- **Response `200 OK`**:
  ```json
  {
    "success": true,
    "station_id": "9E1-9",
    "inserted_measurements": 3,
    "inserted_surveys": 39,
    "cycles_processed": 1
  }
  ```

### `GET /api/stations`
Returns public list of registered stations, their descriptions, and last-seen timestamps.

### `GET /api/station/:id`
Returns detailed station metadata, latest monitored network measurements, visible 2.4 GHz spectrum survey, 24-hour raw history, and 30-day daily rollups.

### `GET /api/export?station_id=:id&format=csv|json`
Downloads recent telemetry for analysis or archival into the `data/` repository directory.

---

## Administrative APIs (Protected by HMAC Bearer Token)

- `POST /api/admin/login` — Administrator authentication & session issuance
- `GET /api/admin/verify` — Admin session validation
- `GET /api/admin/overview` — D1 storage metrics, table row counts, operational span, and yearly activity heatmap (`?year=YYYY&station_id=ID`)
- `POST /api/admin/station/update` — Update node description, canary OTA release track, and target firmware version
- `POST /api/admin/station/delete` — Decommission node with optional cascade telemetry purge
- `POST /api/admin/data/delete` — Timeframe-based raw measurement and survey pruning

---

## Network Diagnostics & Firewall Monitoring (v0.7.0)

Version `0.7.0` introduces real-time gateway and dual-stack DNS monitoring alongside automated daily security and firewall audits:

1. **Real-Time Measurement Additions (Every 10-Minute Polling Cycle)**:
   - **Gateway RTT**: Measures local TCP SYN-ACK RTT to default gateway (`WiFi.gatewayIP()`). Immediately differentiates local AP/firewall bottlenecks from ISP uplink degradation.
   - **Dual-Stack DNS Resolution (IPv4 `A` vs IPv6 `AAAA`)**: Measures latency to resolve hostnames via IPv4 vs IPv6. Accurately detects broken dual-stack networks where upstream routers advertise IPv6 DNS but blackhole traffic, causing 30-second application timeouts.
   - **Captive Portal / HTTP Canary Trap**: Detects HTTP redirection or 302/200 hijack when probing standard canary endpoints (`connectivitycheck.gstatic.com/generate_204`).

2. **Automated Daily In-Depth Diagnostics (Every 24 Hours)**:
   - **Gateway & Firewall Inspection**: Probes gateway port 80 & 443 to detect web management interfaces (`pfSense`, `OpenWrt`, `OPNsense`, or generic router WebUIs) and checks whether HTTP-to-HTTPS redirect traps are present.
   - **Path MTU (PMTUD) & MSS Clamping**: Sweeps packet reachability across 1500 (Standard), 1492 (PPPoE), and 1420 (VPN) MTUs to diagnose silent packet drops and TLS handshake stalls.
   - **DNS Rebinding Protection**: Audits whether public queries resolving to private RFC 1918 ranges are suppressed.
   - **Egress Firewall Rules**: Validates outbound connectivity to critical ports (DNS 53, HTTP 80, HTTPS 443, NTP 123, SSH 22, DoT 853).

3. **Dashboard Summary & Expandable View**:
   - High-level status bar with color-coded pills for Gateway RTT, Firewall Detection (e.g. pfSense Exposed vs Secured Gateway), Dual-Stack DNS Health, and Captive Interception.
   - Expandable **Detailed Audit Drawer** displaying comprehensive 4-quadrant diagnostic breakdowns for all station types.
   - Integrated **Gateway RTT** plotted alongside Google, Cloudflare, Microsoft, Netflix, and Facebook on the 24-hour latency trend graphs.

---

## 2.4 GHz Promiscuous Spectrum Congestion & Scan Time Analysis (v0.8.0)

Version `0.8.0` introduces RF channel utilization and contention measurement via ESP-IDF promiscuous mode, resolving the limitation of only tracking station count:

1. **Promiscuous Mode Channel Contention Profiling (Every 30 Minutes)**:
   - **13-Channel Full Spectrum Sweep**: Sweeps 2.4 GHz channels 1 through 13 with a 2.0-second dwell per channel (26 seconds total measurement time).
   - **Traffic Classification**: Separately tallies 802.11 **Management frames** (beacons, probe requests/responses), **Data frames** (QoS & user payloads), and **Control frames** (RTS, CTS, ACK, BlockACK).
   - **Unique Transmitter Identification**: Extracts 802.11 Transmitter Addresses (TA / SA) from frame MAC headers to quantify active competing transmitters per channel.
   - **RSSI Histogram Distribution**: Categorizes packet signal strength into 4 buckets (`< -80 dBm`, `-80 to -71 dBm`, `-70 to -61 dBm`, `>= -60 dBm`).
   - **PHY-Modeled Airtime Utilization %**: Computes channel airtime consumption based on PLCP preamble duration, packet length, and modulation coding scheme (Legacy OFDM/DSSS or 802.11n HT MCS 0–7) with SIFS/ACK interframe spacing.

2. **Spectrum Scan Duration Variance Tracking (Every 10 Minutes)**:
   - Records the exact duration in milliseconds required for `WiFi.scanNetworks(false, true)` across all available channels.
   - Correlates scan time variance with surrounding RF congestion and channel density.

3. **Interactive 24-Hour Dashboard Visualization**:
   - **Channel View Selector**: Instant toggles between:
     - **All Channels Combined**: Overall average 2.4 GHz airtime consumption curve.
     - **Channels 1, 6, 11 (Non-Overlapping)**: Isolates the primary non-overlapping channels in Sky Blue, Amber, and Emerald.
     - **All Channels Individual (1–13)**: Displays 13 distinct channel lines with dedicated color mapping.
   - **Scan Duration Overlay**: Optional secondary Y-axis overlay plotting spectrum scan duration (ms) against channel utilization.
   - **13-Channel Breakdown Matrix Table**: Expandable drawer with per-channel progress bars, frame type breakdowns, unique transmitters, and 4-tier stacked RSSI distribution mini-bars.

---

## Over-The-Air (OTA) Updates & In-Memory Offline Buffering (v0.9.0)

Version `0.9.0` introduces automated Over-The-Air firmware updates, single unified ESP32 binary distribution, and in-memory zero-wear flash buffering:

1. **Dual-OTA with Large APP Partition Scheme (`min_spiffs.csv`)**:
   - Each OTA application slot (`app0` and `app1`) is expanded to **1.9 MB (1,966,080 bytes)**, providing ample headroom for feature additions.
   - Preserves 192 KB for an ultra-compact raw binary offline data buffer.

2. **Single Unified ESP32 Binary with Runtime Hardware Configuration**:
   - Compiles a single universal `.bin` for all ESP32 nodes.
   - Hardware target profiles (LILYGO T18, LILYGO T8, ESP32 DevKit V1, and Custom) are selected at runtime during first-boot Captive Portal setup and saved to **NVS (`Preferences`)**.
   - Dynamically configures:
     - Status LED GPIO pin (e.g. GPIO 5 for T18, GPIO 21 for T8, GPIO 2 for DevKit).
     - Battery ADC pin and LiPo voltage divider ratio (0.5 for T8/T18, 0.33 for future ESP32-C5).
     - Future capabilities (5 GHz band, Wi-Fi 6 flags).

3. **In-Memory-First Telemetry (Zero Flash Wear)**:
   - When the internet connection is healthy, telemetry is transmitted directly from RAM to Cloudflare. **Flash memory is never touched**, preventing flash wear.
   - **Offline Binary Spooling**: If Wi-Fi or API upload fails, records are serialized into a packed binary format (direct SPI flash block access, zero filesystem overhead) in the dedicated 192 KB raw flash partition (`raw_spool`). Holds over **50+ hours of full telemetry** (13 channels + Wi-Fi surveys) before wrapping.
   - When network connectivity is restored, backlog records are automatically transmitted and the flash buffer is cleared.

4. **Automated CI/CD & Dual-Hosting OTA Distribution**:
   - **GitHub Actions Workflow**: Automatically compiles firmware on tag push (`v*`), generating `.bin` binaries and SHA256 checksums attached to GitHub Releases.
   - **Cloudflare R2 Bucket**: Primary zero-egress binary storage (`wifi-ota`), with automatic HTTP 302 fallback to GitHub Releases.
   - **Canary Staged Rollouts**: The `/api/ota` endpoint queries Cloudflare D1 to enable per-station canary testing before pushing updates to the full fleet.

---

## License

This project is licensed under the **Apache License 2.0** — see the [LICENSE](LICENSE) file for details.
