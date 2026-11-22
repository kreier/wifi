# Changelog

All notable changes to the `kreier/wifi` platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Version Note:** The platform maintains synchronized semantic versioning across all components:
> - **Firmware:** Embedded application (`v1.1.x`) for ESP32, ESP32-C3, ESP32-S3, and ESP32-C5
> - **Web & Worker:** Cloudflare Worker edge backend and Web Dashboard (`v1.1.x`)

---

## [Unreleased]

---

## [1.1.10] - 2026-11-22

### Fixed & Enhanced (OTA Distribution & Zero-Redirect Streaming Proxy)
- **Zero-Redirect GitHub Pages OTA Fallback (`ota.ts`)**:
  - Added architecture-aware binary mapping (`firmware-esp32(-v*).bin` -> `firmware.bin`, `esp32c3` -> `firmware-c3.bin`, `esp32s3` -> `firmware-s3.bin`) for GitHub Pages static mirror fallback.
  - Replaced HTTP 302 redirects with direct 200 octet-stream responses, preventing ESP32 `HTTPUpdate` TLS re-negotiation aborts when GitHub Releases assets are pending or unavailable.
  - Eliminated redirect loops on unmapped release assets.
- **CI/CD Firmware Build & Release Resiliency (`build-release.yml`)**:
  - Made ESP32-C5 build non-blocking (`|| true`) until upstream PlatformIO package registry support is published, preventing build workflow failures from blocking GitHub Releases publication for ESP32, ESP32-C3, and ESP32-S3.
  - Switched release asset upload pattern to wildcard `release_assets/*.bin`.

### Enhanced (Network Probing & Socket Lifecycle)
- **Isolated HTTP Probe Sockets (`latency_prober.h`)**:
  - Dedicated ephemeral `WiFiClient` instances per global probe target with explicit socket closure (`client.stop()`), eliminating stale TCP state between probe targets.
- **Optimized Multi-Port Gateway Latency Probing**:
  - Reset latency timer `t0` per port attempt across ports 80, 443, and 53 with 400ms per-port timeout, preventing TCP SYN timeout on filtered port 80 from distorting RTT on port 443.

---

## [1.1.9] - 2026-11-21

### Added & Enhanced (Web Dashboard & 7-Day Heatmaps)
- **7-Day Historical Heatmap Mode Toggle (Max / Average)**:
  - Added interactive `[Max (Peak)]` / `[Average]` toggle button group to the 7-Day Historical Heatmaps header.
  - User preference is automatically saved to browser `localStorage` (`wifi_weekly_heatmap_mode`) and preserved across visits.
  - **Wi-Fi Spectrum Matrix**: In Average mode, calculates true arithmetic mean channel airtime per 30-minute interval across the 7-day period with adjusted color scaling and rich tooltips (`Avg: X.X% | Peak: Y.Y% (Ch Z)`).
  - **Bluetooth Matrix**: In Average mode, computes dynamic scale thresholds and colors cells by average device density rather than burst peak, updating the legend and badge (`Scale: Peak Avg N devices`).
  - Filtered unmeasured placeholder channel entries (`airtime_pct == 0 && data_frames == 0 && mgmt_frames == 0 && ctrl_frames == 0 && !scan_duration_ms`) across 24h spectrum survey tables and weekly matrix renderers.

### Fixed (Firmware Channel History & Battery Cadence)
- **Resolved Periodic Zero-Airtime Dip & Half-Averaging Anomaly (`channel_history.h`)**:
  - Fixed battery-cadence intermediate cycles (`is30mBoundary == false`) where 13-channel promiscuous scans are skipped: `sample_count` is no longer incremented for empty intermediate cycles, preventing premature generation of dummy 0% airtime records.
  - Fixed rolling slot seeding: the first real measurement on a 30-minute boundary directly initializes the slot values rather than averaging against zero (which previously caused real 15.4% airtime measurements to be halved into 7.7% / 7.5%).
  - Guarded `submitPendingHistory()` to require `sample_count > 0`, ensuring only valid measured channel data is staged and transmitted.

### Enhanced (Network Probing & Firewall Compatibility)
- **HTTP Probe Redirect Handling (`latency_prober.h`)**:
  - Switched `HTTPClient` redirect policy to `HTTPC_DISABLE_FOLLOW_REDIRECTS`.
  - When probe targets (such as `facebook.com` or `fast.com`) return HTTP 301/302 redirects to HTTPS, the probe records the valid TCP and HTTP response round-trip time without throwing an unencrypted SSL protocol error.
- **Gateway Probe Firewall Traversal**:
  - Expanded gateway latency probing to attempt TCP port **443** (HTTPS) in addition to port 80 and 53, enabling latency measurement on routers that restrict LAN management to HTTPS or drop plaintext HTTP SYN packets.
- **DNS Resolver Robustness**:
  - Updated POSIX `getaddrinfo()` socket type hints from `SOCK_STREAM` to `0` (any socket type) with automated fallback to `WiFi.hostByName()`, preventing lookup failures on strict router DNS implementations.

---

## [1.1.8] - 2026-11-20

### Added & Enhanced (Multi-Architecture Firmware & Hardware Support)
- **Multi-Architecture Build Matrix (ESP32, ESP32-C3, ESP32-S3, ESP32-C5)**:
  - Added distinctive, optimized builds across 4 microcontroller architectures:
    1. **Standard ESP32** (`[env:esp32dev]`): Xtensa dual-core (LILYGO T18, T8, DevKit V1, custom boards) with dual 2.4 GHz Classic Bluetooth (BR/EDR) and BLE.
    2. **ESP32-C3 Super Mini** (`[env:esp32c3]`): RISC-V single-core, active-low GPIO 8 status LED, native USB CDC, lightweight build without Classic BT (`lib_ignore = BluetoothSerial`).
    3. **ESP32-S3** (`[env:esp32s3]`): Xtensa dual-core, native USB CDC, BLE (Classic BT omitted).
    4. **ESP32-C5** (`[env:esp32c5]`): RISC-V single-core, **Wi-Fi 6 (802.11ax)** with **Dual-Band 2.4 GHz and 5 GHz spectrum support**, and BLE (Classic BT omitted).
  - Classic Bluetooth (`BluetoothSerial.h`) is physically absent from ESP32-C3, ESP32-S3, and ESP32-C5 silicon; conditional compilation guards (`HAS_CLASSIC_BT`) eliminate unused code, saving >400 KB flash space and reducing RAM footprint across all three modern chips.
  - Adapted deep sleep and light sleep wakeups for RISC-V targets using `gpio_wakeup_enable` and `esp_sleep_enable_gpio_wakeup()`.
  - Added hardware presets for ESP32-C3, ESP32-S3, and ESP32-C5 to the captive portal Soft-AP configuration interface (`captive_portal.h`).

### Added & Enhanced (Power Efficiency & Offline Storage)
- **Automatic Energy-Preserving Switch on Power Bank Keep-Alive**:
  - When `powerbank_keepalive` is enabled (even on boards without battery voltage sensing like the ESP32-C3 Super Mini where Vbat appears permanently powered), the firmware automatically switches from 2-minute cadence to battery-optimized 10-minute cadence.
  - Promiscuous channel congestion surveys and network latency probing are automatically throttled to the 30-minute boundary (`is30mBoundary`), reducing radio active duty cycle from 27.7% to only ~3%.
  - Optimized dwell time per channel: reduced from 2000 ms to 800 ms per channel in battery/power bank modes, saving 15.6 seconds of active radio time per 30 minutes.
  - Extended keep-alive wake pulse interval to 20 seconds with a 120 ms high-current burst, boosting ESP32-C3 power bank runtime to **62.7+ days** on a 10,000 mAh power bank.
- **18-Day Zero-Wear Offline 30-Minute Ring Buffer**:
  - Intermediate cycles accumulate solely in RAM; only once every 30 minutes at the boundary is a consolidated slot persisted to flash ring buffer (`spool_manager.h` and `channel_history.h`).
  - Streamlined binary serialization: 30-minute offline records retain essential 30-minute metrics (airtimes, unique transmitters, monitored network reliability, BLE unique devices, and battery mV), shrinking records to ~110-120 bytes.
  - 176 KB circular flash buffer holds **1,500+ records**, guaranteeing **over 31 days** (well exceeding the 18 days requirement) of offline telemetry during network blackouts while limiting flash wear to at most 48 writes/day.
  - Transactional batch confirmation: `SpoolManager::confirmBatchUploaded()` advances `read_offset` only after HTTP 200 ingestion confirmation, retrying safely across network dropouts.
- **Comprehensive Power Model Documentation**:
  - Added `docs/power-consumption.md` detailing mathematical modeling, measured light sleep benchmarks, and hardware runtime projections.

### Added & Enhanced (Web Dashboard & Worker Backend)
- **Randomized BLE MACs Periodic Drop Fix**:
  - `ble_random_macs` is now tracked, stored, and serialized across 30-minute intervals in `channel_history.h`, resolving cyclical zero-dips in time-series charts.
  - Client-side interpolation in `frontend.ts` and database `ON CONFLICT` preservation for legacy records.
- **Dynamic BLE Heatmap Color Scaling**:
  - Replaced the hard-coded `> 25 devices` saturation ceiling with runtime peak-detection (`dynamicBleMax = Math.max(10, Math.ceil(maxBle / 5) * 5)`).
  - Dynamically computes thresholds across color palettes (Spectrum, Inferno, Viridis, Plasma), providing high-contrast visual distribution for both quiet and dense RF areas.
- **Outlier Detection & Telemetry Cleanup Engine**:
  - Added backend SQL anomaly scanning engine (`db.scanOutliers()`) and dedicated **🛡️ Outliers** tab in Settings & Fleet Management.
  - One-click fleet-wide or per-station scanning with multi-select checkbox deletion and atomic batch purge (`/api/admin/outliers/delete`).
- **Web Serial Flasher & Architecture Selector**:
  - Added interactive hardware target selector to the Web Flasher modal supporting Standard ESP32, ESP32-C3, ESP32-S3, and ESP32-C5.
  - Added dedicated manifests (`manifest-esp32.json`, `manifest-c3.json`, `manifest-s3.json`, `manifest-c5.json` and `-update` variants) for one-click in-browser flashing.
- **Dedicated Station Configuration Modal & Canary Gating**:
  - Added `#editStationModal` dialog with "⚙️ Configure" shortcut button on station header.
  - Added semantic version comparison to gate Canary enrollment on eligible firmware versions with direct controls for description modification and OTA version pinning.
- **Architecture-Aware OTA Streaming Proxy**:
  - Updated `/api/ota` and binary streaming proxy to inspect `board` query parameter and deliver architecture-specific binaries (`esp32`, `esp32c3`, `esp32s3`, `esp32c5`).

---

## [1.1.4] - 2026-11-19

### Added & Enhanced (Canary Release Channel & Fleet Management)
- **Canary Firmware Enrollment & NVS State**:
  - Added persistent `is_canary` flag to `AppConfig` and `AppConfigManager` (`setCanaryTrack`, `isCanaryTrack`) stored in ESP32 NVS.
  - Added "Canary / Beta Release Channel" enrollment checkbox to captive portal setup UI (`captive_portal.h`) and serialized `is_canary` into telemetry cycle payloads.
- **Unified OTA Firmware Update Check & Dual-Verification**:
  - Combined OTA check (`GET /api/ota`) with canary entitlement verification and bidirectional synchronization.
  - Server reports station canary entitlement (`is_canary`) and targeted release channel (`track: "canary" | "stable"`).
  - Firmware automatically synchronizes local NVS `is_canary` preference if updated by administrator in Cloudflare D1.
  - Independent firmware verification safeguard: firmware refuses to flash canary / beta candidate binaries unless device is explicitly enrolled in the canary channel.
  - Enhanced semantic version parsing and comparison in both Cloudflare Worker backend (`worker/src/ota.ts`) and embedded firmware (`ota_manager.h`) to support pre-release tags (e.g. `1.1.4-canary.1`).
- **Dashboard & Admin Visual Indicators**:
  - Displayed prominent amber `CANARY` badge on station header and fleet management overview table when a station is enrolled in canary testing.
  - Added `CANARY_VERSION` environment configuration to `wrangler.toml` enabling independent canary fleet deployments alongside `STABLE_VERSION = "1.1.4"`.

---

## [1.1.3] - 2026-11-19

### Fixed & Optimized (Firmware)
- **Wi-Fi Spectrum Scan Duration**:
  - Corrected `_ramScanDurationMs` in `app_main.h` to record individual scan duration (~4000 ms) instead of cumulatively summing durations across 5 two-minute subcycles (~20,000–25,000 ms) in powered mode.
  - Removed erroneous hardcoded `scan_duration_ms: 26000` from 30-minute promiscuous airtime batches in `channel_history.h`.
- **Bluetooth Scan Count & Discovered Device List**:
  - Increased linear unique device tracking buffer in `BluetoothScanner::scanBle` from 25 to 60 records, resolving fixed 25-device saturation and restoring true RF count accuracy (20–45+ devices).
  - Increased reported device details from 15 to 20 in `BluetoothScanner` and allowed up to 25 detailed records in `_ramBt.ble_devices`.

### Fixed & Enhanced (Worker Backend & CI/CD)
- **Bluetooth Device Retention in D1**:
  - Updated `saveBluetoothTelemetry` in `db.ts` with conditional coalesce logic so that 30-minute aggregate rollup slots (which do not carry individual MAC lists) never overwrite or zero out `devices_json` in `station_latest_bluetooth` or `station_bluetooth_summary`.
  - Added upper-bound validation (`< 15000 ms`) in `telemetry.ts` to prevent anomalous scan times from overwriting `latestScanDuration`.
- **Release Documentation & Flash Sizing**:
  - Enhanced GitHub Actions release workflow (`build-release.yml`) to automatically parse and publish exact Flash memory utilization (bytes and percentage of 1.9 MB OTA partition), dynamic RAM usage, release highlights, and flashing guides in GitHub Releases.

---

## [1.1.2] - 2026-11-19

### Added & Enhanced (Web Dashboard & Worker Backend)
- **Timeframe Selector for Channel Utilisation**:
  - Renamed section to **Channel Utilisation & Spectrum Congestion**.
  - Added timeframe selector pill buttons (`24h`, `48h`, `7d`) filtering measurement history to user-selected duration and formatting X-axis timestamps appropriately (`HH:MM` for 24h, `MM/DD HH:MM` for 48h/7d).
- **7-Day Historical Heatmaps (Wi-Fi Bands & Bluetooth)**:
  - Added collapsible drawer under the main heatmap for the last 7 days partitioned into 48 discrete 30-minute intervals (00:00 to 24:00).
  - Displays two distinct 7x48 heatmaps: **All Wi-Fi Bands** (peak/average airtime %) and **Bluetooth** (unique detected BLE + Classic devices).
  - Interactive cell inspection showing exact date, 30-minute time window, and metrics on hover/click.
- **Dynamic Intensity Color Schemes**:
  - Added heatmap palette selector supporting 4 distinct color schemes:
    - **Spectrum** (Emerald / Amber / Red-Violet / Indigo)
    - **Inferno** (Black / Violet / Warm Orange / Yellow)
    - **Viridis** (Purple / Teal / Vibrant Green / Yellow)
    - **Plasma** (Navy / Magenta / Orange / Bright Yellow)
  - Color palettes dynamically update both 24h/multi-day utilization heatmaps, weekly summary heatmaps, and their respective legend swatches, persisting user choice to `localStorage`.
- **Database & Fleet Reliability**:
  - Applied migration `0011_daily_30min_summaries_and_diagnostics_history.sql` to remote D1 `wifi-mon-db` creating `daily_30min_summaries`.
  - Added defensive table querying and structured error messaging to `/api/admin/overview` and frontend admin loader to prevent UI hanging on database errors.
  - Extended API retention query for channel utilization and Bluetooth metrics from 24 hours to 168 hours (7 days).

---

## [1.1.1] - 2026-11-18

### Fixed & Optimized (Firmware)
- **Bluetooth Scan Heap Exhaustion & OOM Panic Resolution**:
  - Automatically shuts down the Wi-Fi radio (`WiFi.mode(WIFI_OFF)`) and frees scan memory immediately after the Wi-Fi spectrum survey completes, reclaiming >40 KB of internal DRAM for the Bluedroid BLE/Classic stack.
  - Added defensive free heap threshold guards in `BluetoothScanner::scanBle` (min 40 KB) and `BluetoothScanner::scanClassic` (min 35 KB) to eliminate fatal `std::bad_alloc` aborts in crowded 2.4 GHz RF environments.
  - Replaced associative heap containers (`std::map<String, BleDeviceRecord>` and `std::set<String>`) with linear deduplication in `std::vector` and zero-allocation 64-bit integer MAC hashing, eliminating >600 heap allocations and RB-tree node fragmentation per scan.
  - Strictly capped unique tracked BLE devices in scan phase to 25 records.
  - Capped RAM accumulation across 2-minute subcycles in powered mode: `_ramSurvey` capped at 60 APs, `_ramBt.ble_devices` capped at 15, and `_ramBt.classic_devices` capped at 10.
  - Added explicit scoping for Wi-Fi scan results vector in `app_main.h` ensuring deallocation before Bluetooth scanning initiates, and added `shrink_to_fit()` upon 10-minute cycle reset.

### Added & Enhanced (Web Dashboard & Worker Backend)
- **24-Hour Station Reliability Timeline (48 × 30-Minute Interval Bar)**:
  - Interactive 48-segment status bar embedded inside each network card below HTTP latency probes with color-coded failure categorization: healthy (green), HTTP probe timeout (purple), DNS resolution timeout (amber), gateway RTT timeout (orange), DHCP lease timeout (red), association failure (charcoal), or idle (gray).
- **Collapsible Scan Surveys**:
  - Surrounding 2.4 GHz Spectrum Survey and Previously Detected Networks tables are now collapsible with interactive expand/collapse headers, collapsed by default for streamlined viewing.
- **Settings & Fleet Management Console**:
  - Secure administrator suite with HMAC-SHA256 session tokens (`/api/admin/*`) backed by Cloudflare Worker environment secrets.
  - Dynamic D1 database sizing breakdown across all 13 tables with estimated row counts and storage metrics.
  - Station administration: modify node descriptions, toggle Canary OTA release tracks, or pin explicit target versions.
  - Selective historical data cleanup by station and timeframe.
  - 52-week annual activity heatmap (`.cal-*` namespace to avoid style collisions with 24h spectrum heatmaps).

---

## [1.1.0] - 2026-11-17

### Added
- **Firmware (v1.1.0)**: **Power-Aware Measurement Cadence Matrix** (`app_main.h`):
  - **Battery Powered Mode** ($\le 4.20\text{ V}$): Base 10-minute cycle. 13-channel airtime survey and monitored network latency/speed probing are executed every **30 minutes** to eliminate heavy radio drain and extend 18650 LiPo battery lifetime. Latency probes are strictly performed only for networks detected during the Wi-Fi scan survey.
  - **Permanently Powered Mode** ($> 4.20\text{ V}$ / no battery): 2-minute cycle with peak RSSI, max airtime %, and unique Bluetooth devices accumulated in RAM, uploading 10-minute maximums with latency probes run every 10 minutes.
- **Firmware (v1.1.0)**: **In-Memory 24-Hour 48-Slot Revolving Buffer** (`channel_history.h`):
  - Preserves 48 30-minute intervals of 13-channel airtime, unique BLE devices, Classic BT devices, and battery voltage.
  - **Zero Flash Wear When Online**: When internet connectivity and uploads succeed every 10 minutes, zero bytes are written to flash.
  - **Offline Resilience**: Automatically commits the 48-slot history buffer and spool records to raw flash sectors ONLY when offline or when upload attempts fail, transmitting the backlog once connectivity is restored.
- **Backend & Database (v1.1.0)**: **Migration 0011** (`0011_daily_30min_summaries_and_diagnostics_history.sql`):
  - `daily_30min_summaries`: Daily cron job (`00:15 UTC`) aggregates yesterday's 48 30-minute intervals (channel max airtime, unique BLE max, Classic BT max, battery voltage) per active station.
  - `station_network_diagnostics_history`: Records gateway security audits, PMTUD MTU tests, DNS rebind checks, and open ports long-term on first observation. Subsequent entries are inserted **strictly on configuration/audit changes**, ignoring DNS latency fluctuations.
- **Web UI (v1.1.0)**:
  - 24-Hour Spectrum & BLE Heatmap expanded by default.
  - Battery discharge chart expanded by default (for battery-equipped nodes).
  - Web Flasher dual-mode flashing: **Update Firmware Only (Keep Settings & Data)** via `manifest-update.json` and **Clean Install (Full Flash & Partitions)** via `manifest.json`.
  - Added live Bluetooth KPI metrics for Key Vendors (Apple, Microsoft, Samsung) and Device Types (Smartphone, Computer, Beacon).
  - Removed "Previous 24 Hours" device list in favor of real-time active device inspection.
  - Fixed footer styling, font inheritance, and centered flex layout.
- **Documentation**: Added [ADR 0006: Power-Aware Cadence and 30-Minute Aggregation](docs/adr/0006-power-aware-cadence-and-30min-aggregation.md).

---

## [2.0.8 / 1.0.8] - 2026-11-16

### Changed
- **Firmware (v1.0.8)**: Scan-aware intelligent NTP synchronization and early OTA update workflow in `firmware/include/app_main.h`. Cross-references the Wi-Fi scan survey results to connect only to visible monitored networks, or falls back immediately to the backup AP (e.g. "S24") when primary networks are unavailable. Bypasses connection attempts and 15-second timeouts entirely when neither is in the air.
- **Firmware (v1.0.8)**: Streamlined cycle upload workflow in `processCycleUploadAndOta` to connect directly to detected visible uplinks (monitored or backup AP), preventing delays on absent SSIDs.
- **Firmware (v1.0.8)**: Added backup AP failover in `ensureDeviceRegistered()` to allow initial zero-trust device provisioning via backup hotspot if primary monitored networks are unavailable.
- **Backend & OTA (v2.0.8)**: Updated stable production OTA fleet target (`STABLE_VERSION`) to `1.0.8` in `wrangler.toml` and `ota.ts`.
- **Web UI (v2.0.8)**: Updated brand badge and firmware modal installer targets to `v1.0.8 Stable` / `v2.0.8`.
- **Documentation**: Audited `README.md` and related docs, removing outdated `LittleFS` references and aligning architecture diagrams and descriptions with NVS `Preferences` persistence and 192 KB raw partition binary spooling (`raw_spool`).

---

## [2.0.7 / 1.0.7] - 2026-11-16

### Changed
- **Firmware (v1.0.7)**: Redesigned boot and wakeup sequence to eliminate unnecessary 60-second Soft-AP wait on boot and wake from light sleep.
- **Firmware (v1.0.7)**: Cycle immediately starts with a 13-channel promiscuous congestion survey (`ChannelAnalyzer::measureChannels`).
- **Firmware (v1.0.7)**: Dynamic LED visual feedback during channel scanning: LED is solid ON while dwelling on a channel, and blinks OFF for 100 ms when transitioning to the next channel.
- **Firmware (v1.0.7)**: Non-blocking BOOT button monitoring during the 13-channel scan: tapping or holding the BOOT button at any point immediately terminates the scan and launches the Soft-AP captive portal setup. If BOOT is not pressed, the cycle proceeds directly to spectrum survey, diagnostics, and telemetry transmission.

### Added
- **Firmware (v1.0.7)**: Backup Internet Uplink failover capability (configurable, default SSID: "S24", password: "12345678").
- **Firmware (v1.0.7)**: Automatic detection of backup AP in the Wi-Fi scan survey. If monitored primary Wi-Fi networks have no internet connection or fail to upload telemetry, the station automatically connects to the backup hotspot to transmit queued offline telemetry and current cycle data.
- **Setup Web Portal**: Added **Backup Internet Connection** configuration section in the Captive Portal setup UI to enable/disable backup failover and customize backup SSID and password.
- **Web & Backend (v2.0.7)**: Updated stable firmware version to `v1.0.7` across Worker OTA delivery, Web Serial installer modal, and manifest metadata.
- **Web UI (v2.0.7)**: Fixed hover indicator glitch and flickering on Channel 13 and BLE cells by giving `#heatmapHoverInfo` a dedicated, permanently allocated line below the color indicators, preventing text wrapping from shifting the heatmap.
- **Web UI (v2.0.7)**: Added dedicated **Unique BLE Devices** row to the 24-Hour Spectrum Heatmap, mapping detected BLE devices across 48 half-hour slots with density-based color scaling and detailed hover telemetry inspection.
- **Web UI (v2.0.7)**: Added interactive view switch in **Discovered Bluetooth Devices** drawer between **Current Cycle** and **Previous 24 Hours**, displaying historical BLE and Classic Bluetooth devices with "Active" badges and "Last Seen" timestamps.
- **Backend (v2.0.7)**: Added `getRecentBluetoothDevices` database query and API response payload to retrieve all Bluetooth devices discovered over the past 24 hours.
- **Backend (v2.0.7)**: Added unsynced timestamp fallback in `telemetry.ts` (`< 1700000000`) to guarantee nodes connecting via backup APs before NTP sync record valid 24-hour spectrum utilization.
- **Telemetry & UI (v2.0.7)**: Filtered out floating ADC noise (`< 2500 mV`) so AC/USB-powered nodes without batteries (e.g., DevKit V1) no longer display a false 0% battery indicator.
- **Firmware (v1.0.7)**: Added NTP time synchronization upon failover connection to the backup AP.

---

---

## [2.0.6 / 1.0.6] - 2026-11-16

### Added
- **Firmware (v1.0.6)**: 2.4 GHz **Bluetooth Low Energy (BLE)** active scan (3s) and **Bluetooth Classic (BR/EDR)** inquiry scan (3s) integrated into the scheduled 10-minute cycle (`firmware/include/bluetooth_scanner.h`).
- **Firmware (v1.0.6)**: Deep **BLE Advertising Payload Fingerprinting & De-Randomization Algorithm**:
  - Distinguishes address privacy types: IEEE Public, Random Static, Resolvable Private Address (RPA), and Non-Resolvable Private Address.
  - Inspects AD structures to identify Apple iBeacon (`UUID:Major:Minor`), Google Eddystone (`UID:Namespace:Instance`), Apple AirTags / Find My network beacons, and named BLE peripherals.
  - Clusters anonymous randomized RPAs by vendor company ID (Apple `0x004C`, Google `0x00E0`, Microsoft `0x0006`, Samsung `0x0075`), continuity payload sub-type, service UUIDs, and RSSI spatial zones to determine true unique device presence.
- **Firmware (v1.0.6)**: Decodes Classic Bluetooth Class of Device (COD) descriptors into human-readable device classifications (Audio / Headset, Smartphone, Laptop, Peripheral, Wearable).
- **Firmware (v1.0.6)**: Clean dual-radio sequential coordination and controller deinitialization (`BLEDevice::deinit(false)` and `serialBT.end()`), preserving heap and returning radio cleanly to Wi-Fi.
- **Firmware (v1.0.6)**: Spool manager (`spool_manager.h`) raw binary flash partition persistence for Bluetooth cycle summary when offline.
- **Database (Migration 0010)**: Added `station_bluetooth_summary` and `station_latest_bluetooth` tables tracking BLE unique device counts, total MACs, RPA vs Public distributions, category breakdowns, and device JSON.
- **Backend (v2.0.6)**: Cloudflare Worker API endpoints (`/api/station/:id`) return latest Bluetooth scan data and 24-hour historical records. Daily cron maintenance (`cron.ts`) automatically retains 30 days of Bluetooth history.
- **Web (v2.0.6)**: Dedicated **2.4 GHz Bluetooth & BLE Activity** card in Web UIs:
  - Quick KPI summary tiles: Unique BLE Devices, Total Observed MACs, Address Privacy ratio (RPA vs Public), Classic BT Devices, and Device Composition.
  - Interactive 24-Hour Trend Chart (Chart.js) comparing Unique BLE Devices, Randomized MACs, and Classic Bluetooth over time.
  - Collapsible device drawer listing discovered devices with type badges (`BLE` vs `Classic`), address privacy badges (`RPA`, `Public`, `Static`), friendly names, vendor ecosystem, category, and signal RSSI strength bars.
- **Documentation**: Added [ADR 0005: 2.4 GHz Bluetooth Classic & BLE Ingestion with Advertising Payload De-Randomization](docs/adr/0005-bluetooth-ble-spectrum-and-derandomization.md).

---

## [2.0.5 / 1.0.5] - 2026-11-15

### Added
- **Web (v2.0.5)**: Expandable **24-Hour Channel Utilization Heatmap** drawer displaying all 13 channels across 48 half-hour slots (2 boxes per hour) with an adaptive 13-step nuance color scale emphasizing 5%–30% airtime variation and distinctly highlighting outlier spikes (60% and 80%+).
- **Web (v2.0.5)**: Interactive hover inspector banner displaying time interval, airtime %, exact data/mgmt/ctrl frame counts, and active transmitter count per cell.
- **Database (Migration 0009)**: Deduplicated and normalized all BSSIDs to uppercase across `station_discovered_aps`, `measurements`, and `station_latest_measurements`.
- **Worker (v2.0.5)**: Added automatic exclusion of active scan BSSIDs in `getInactiveSurveys` query engine to prevent any casing or timing race conditions from flagging active APs as missing.

### Fixed
- **BSSID Normalization & Ghost Inactive APs**: Resolved case mismatch bug where mixed lowercase/uppercase hex BSSIDs caused identical physical APs to be split and falsely flagged as "absent" / "missing".
  - **Firmware (v1.0.5)**: Enforced uppercase conversion (`item.bssid.toUpperCase()`) in both `WifiScanner` (`wifi_scanner.h`) and `LatencyProber` (`latency_prober.h`).
  - **Backend (v2.0.5)**: Sanitized all incoming BSSIDs with `.trim().toUpperCase()` in `telemetry.ts` and `db.ts`.
  - **Web Frontend (v2.0.5)**: Normalized BSSIDs to uppercase and dynamically deduplicated inactive networks against active scan BSSIDs.

---

## [2.0.4 / 1.0.4] - 2026-11-15

### Added
- **Web (v2.0.4)**: Dynamic 4-bar SVG battery status icon with calculated percentage placed immediately to the left of the online badge in the station banner.
- **Web (v2.0.4)**: Automated battery discharge trend analysis and remaining runtime estimation:
  - `"Running on battery for XXXh (DD days X hours)"`
  - `"Estimated runtime YYYh (DD days Y hours)"`
- **Web (v2.0.4)**: Collapsible battery discharge graph drawer with interactive Chart.js line graph and timeframe selector buttons: **Day (24h)**, **Week (7d)**, and **Month (30d)**.
- **Web (v2.0.4)**: Summary KPI cards: Current Voltage, Battery Level %, Discharge Rate ($\text{mV/h}$), Estimated Runtime, and Voltage Range ($\text{Min} - \text{Max}$).
- **Database (Migration 0008)**: Added `station_battery_history` table (`station_id`, `timestamp`, `battery_mv`) and `battery_mv` column on `measurements` and `station_latest_measurements`.
- **Backend**: Daily cron maintenance (`cron.ts`) automatically prunes expired battery history records older than 30 days.
- **Firmware (v1.0.4)**: Configurable **Power Bank Keep-Alive** workaround: wakes for 500 ms with an active current draw every 20 seconds during light sleep, preventing commercial USB power banks from automatically shutting off between 10-minute cycles.
- **Firmware (v1.0.4)**: Opt-in Power Bank Keep-Alive toggle switch in captive portal setup UI, persisted via NVS (`Preferences`).
- **Firmware (v1.0.4)**: Instant BOOT button wake-up interrupt maintained across all keep-alive sleep sub-intervals.
- **Documentation**: Added [ADR 0004: Automated Fleet OTA Updates, Action Runner Modernization, and Battery Telemetry History](docs/adr/0004-fleet-ota-and-battery-monitoring.md).

### Changed
- **CI/CD**: Modernized GitHub Actions workflows (`build-release.yml` and `deploy-pages.yml`) to Node.js 24 (`actions/checkout@v5`, `actions/setup-python@v6`, `softprops/action-gh-release@v3`).
- **Documentation**: Replaced static hard-coded `1.0.0` version badge in `README.md` with dynamic Shields.io release tracking endpoint.

### Fixed
- **Firmware (v1.0.4)**: Added automatic 1-second retry backoff upon transient HTTPS socket failures (`httpCode < 0`) in `OtaManager::checkForUpdate`, preventing spurious `-1` connection drops during Wi-Fi association.
- **Firmware (v1.0.4)**: Explicitly disabled socket reuse (`http.setReuse(false)`) on ESP32 OTA check client to ensure clean TCP/TLS teardown.

---

## [2.0.3 / 1.0.3] - 2026-11-15

### Added
- **Firmware (v1.0.3)**: 60-second configuration captive portal window launched automatically on every boot/reset with soft-AP `WIFI-Mon-XXXX` at `192.168.4.1`.
- **Firmware (v1.0.3)**: Instant BOOT button press bypass during the 60s boot portal window to proceed directly to measurement cycle.
- **Web (v2.0.3)**: Single consolidated Network Environment & Firewall Diagnostics card with interactive monitored network dropdown selector.

### Changed
- **Web (v2.0.3)**: Connected utilization graph line across missing 30-minute polling gaps using `spanGaps: true`.

---

## [1.1.0 / 1.0.2] - 2026-11-15

### Added
- **Firmware (v1.0.2)**: Daily local network and firewall diagnostics extended across all configured monitored SSIDs sequentially.
- **Web (v1.1.0)**: Station banner box displays running firmware version badge (e.g. `v1.0.2`).
- **Telemetry**: Synchronized station location description from firmware config into backend D1 station records.

---

## [1.0.1] - 2026-11-15

### Added
- **Firmware**: Self-healing boot OTA update check executed immediately upon initial Wi-Fi connection prior to long measurement cycles.

### Fixed
- **Firmware**: Replaced Arduino `WiFiUdp` with thread-safe POSIX BSD sockets (`sys/socket.h`) inside dual-stack DNS probing, eliminating multithreaded lwip core lock assertion crashes.
- **Web**: Fixed embedded JavaScript syntax error affecting GitHub Pages mirror execution; added client-side station routing fallback.

---

## [1.0.0] - 2026-11-15

### Added
- **Firmware**: Station firmware version (`FIRMWARE_VERSION`) transmitted in cycle telemetry payloads.
- **Web**: Station header banner displays version badge; station select dropdown remains clean with station ID and location.

### Fixed
- **Backend**: Resolved station rename token reset bug where station ID changes in captive portal invalidated authentication tokens.

---

## [0.9.0] - 2026-11-15

### Added
- **Firmware**: Unified multi-board firmware into single compiled ESP32 binary (`esp32dev`).
- **Firmware**: Dynamic hardware profiles (LILYGO T18, T8, ESP32 DevKit V1, Custom) selectable via captive portal and saved in NVS (`Preferences`).
- **Firmware**: Partition scheme switched to `min_spiffs.csv`, providing two 1.9 MB application slots (`app0`, `app1`) for dual Over-The-Air (OTA) updates.
- **Firmware**: In-memory-first telemetry transmission to prevent flash wear; zero-flash-wear normal cycles with 192 KB raw partition binary buffer fallback.
- **Firmware**: `OtaManager` client with automated background flash updates.
- **Backend (Migration 0007)**: Added `stations.firmware_version`, `stations.target_version`, `stations.board_type`, `stations.battery_mv`, `stations.is_canary`, and `stations.ota_url`.
- **Backend**: Cloudflare Worker `/api/ota` endpoints supporting fleet defaults and per-station Canary targeting, serving binaries via streaming proxy.
- **CI/CD**: Added `.github/workflows/build-release.yml` automating firmware compilation and asset publishing on version tag pushes.

---

## [0.8.0] - 2026-11-15

### Added
- **Firmware**: 13-channel promiscuous 2.4 GHz spectrum analyzer (`ChannelAnalyzer`) with 2-second dwell per channel running every 30 minutes.
- **Firmware**: Frame classification (Management, Data, Control), 4-bracket RSSI histogram (`<-80`, `-80..-71`, `-70..-61`, `>=-60 dBm`), unique transmitter MAC tallies, and PHY airtime percentage modeling.
- **Firmware**: Full Wi-Fi spectrum scan duration tracking in milliseconds.
- **Database (Migration 0006)**: Added `station_channel_utilization` table and `scan_duration_ms` on measurements and scans.
- **Web**: 24-Hour Channel Utilization & Spectrum Congestion chart with view modes for All Channels Combined, Channels 1/6/11 (Non-Overlapping), and Channels 1–13 Individual.
- **Web**: Optional scan duration overlay curve and 13-channel breakdown table with stacked RSSI distribution bars.

---

## [0.7.0] - 2026-11-15

### Added
- **Firmware**: Automated daily network diagnostics (`LatencyProber::runDailyDiagnostics`):
  - Gateway TCP SYN RTT probe (port 80/443).
  - Dual-stack DNS benchmark comparing IPv4 vs. IPv6 lookup latency.
  - HTTP captive portal canary hijacking detection.
  - Gateway web admin exposure audit and port scanner.
- **Database (Migration 0005)**: Added `station_network_diagnostics` table; added `gateway_ms`, `dns_ipv4_ms`, and `dns_ipv6_ms` to `measurements`.
- **Web**: Expandable Detailed Audit drawer in network cards.

---

## [0.6.0] - 2026-11-15

### Added
- **Database (Migration 0002)**: Optimized storage schema with `station_overview_cache`, `station_latest_scan`, `station_discovered_aps`, and `station_latest_measurements`.
- **Backend**: Two-tier caching (Cloudflare isolate in-memory cache + D1 `station_overview_cache` with 10-minute validity) reducing D1 reads by >90%.
- **Database (Migration 0003 & 0004)**: Dropped legacy `scan_surveys` table; deduplicated measurements and enforced unique constraint on `(station_id, ssid, timestamp)`.

---

## [0.5.0] - 2026-11-14

### Added
- **Web**: Light and Dark mode theme toggle with instant stylesheet transition and `localStorage` persistence.

---

## [0.4.0] - 2026-11-14

### Added
- **Web**: Monitored network latency trend filtering (Combined view vs. individual SSID selection).
- **Web**: 6 individual probe latency cards: Gateway RTT, Cloudflare (1.1.1.1), Google (generate_204), Microsoft (connecttest), Netflix (fast.com), Facebook (connectivity).
- **Web**: Peak 24-hour signal strength and quality indicators for inactive/missing access points.

---

## [0.3.0] - 2026-11-14

### Added
- **Backend & Web**: Previously detected networks tracking for surrounding APs absent from the latest cycle.
- **Firmware**: Improved 802.11g PHY mode and WEP/WPA encryption detection in Wi-Fi scan engine.

---

## [0.2.0] - 2026-11-13

### Added
- **Web**: GitHub Pages automated static client deployment workflow (`deploy-pages.yml`).
- **Web**: Direct station deep-linking (`https://wifi.kreier.workers.dev/:stationId` and `?station=:stationId`).

---

## [0.1.0] - 2026-11-13

### Added
- Initial project architecture and telemetry fleet release.
- Dual-build layout for ESP32 in `firmware/` (PlatformIO and Arduino IDE).
- 10-minute automated measurement cycle with light sleep power management.
- Captive portal bootstrap commissioning via Soft-AP `WIFI-Mon-XXXX`.
- Zero-trust device token registration (`POST /register`) with one-time `MASTER_PHRASE`.
- Cloudflare Worker backend and D1 database initial schema (Migration 0001).
- Basic web dashboard with latency cards and 2.4 GHz spectrum scan survey table.
