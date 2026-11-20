# AGENTS.md — AI Agent Guidance & Architectural Reference

This document serves as the primary technical specification and operational playbook for autonomous AI coding agents (such as Antigravity, Cursor, Copilot, Cline, and Claude Dev) working on the `kreier/wifi` repository.

---

## 1. System Overview & Architecture

`kreier/wifi` is an edge-native, zero-trust telemetry fleet monitoring system for 2.4 GHz Wi-Fi spectrum quality, network latency, and RF contention:

```
                      ┌──────────────────────────────────────────────┐
                      │             Deployed ESP32 Nodes             │
                      │  (LILYGO T18, T8, DevKit V1, Custom Boards)  │
                      └──────────────────────┬───────────────────────┘
                                             │
                   HTTPS Telemetry Cycle     │ Every 10 minutes
                   Bearer Token Auth         ▼
                      ┌──────────────────────────────────────────────┐
                      │          Cloudflare Worker (Edge)            │
                      │          wifi.kreier.workers.dev             │
                      ├──────────────────────┬───────────────────────┤
                      │  Telemetry Ingestion │  Streaming OTA Proxy  │
                      │  Token Verification  │  Web Dashboard SSR    │
                      └──────────┬───────────┴──────────┬────────────┘
                                 │                      │
             Indexed SQL Queries │                      │ Releases Fallback
                                 ▼                      ▼
                      ┌──────────────────────┐   ┌───────────────────┐
                      │    Cloudflare D1     │   │  GitHub Releases  │
                      │    wifi-mon-db       │   │  (Firmware Bin)   │
                      └──────────────────────┘   └───────────────────┘
                                                        ▲
                                                        │ Automated Static Mirror
                                                 ┌──────┴────────────┐
                                                 │   GitHub Pages    │
                                                 │ kreier.github.io  │
                                                 └───────────────────┘
```

* **Edge Nodes (Firmware)**: ESP32 microcontrollers operating unattended on battery, USB, or solar power. Every 10 minutes, nodes wake from light sleep, run 2.4 GHz scans, promiscuous channel contention surveys, network latency probes, and battery sensing, uploading telemetry before returning to light sleep.
* **Edge Backend (Worker & D1)**: Cloudflare Worker written in TypeScript backed by Cloudflare D1 (serverless SQLite). Handles device registration, bearer token validation, telemetry batch ingestion, two-tier caching, automated 30-day data retention, and zero-redirect OTA streaming proxies.
* **Dual Dashboards (Web)**: Server-rendered dashboard on Cloudflare Worker (`wifi.kreier.workers.dev`) and static mirror on GitHub Pages (`kreier.github.io/wifi`) driven by a single-source frontend engine in `worker/src/frontend.ts`.

---

## 2. Repository Layout & Component Map

```
kreier/wifi/
├── .github/
│   └── workflows/
│       ├── build-release.yml      # CI/CD: Builds firmware and publishes GitHub Releases (Node 24)
│       └── deploy-pages.yml       # CD: Deploys ./docs to GitHub Pages (Node 24)
├── docs/
│   ├── index.html                 # Pre-built dashboard mirror for GitHub Pages
│   ├── 404.html                   # Routing fallback for deep-linked station URLs
│   └── adr/                       # Architectural Decision Records (ADRs 0001–0005)
├── firmware/
│   ├── platformio.ini             # Unified PlatformIO build configuration ([env:esp32dev])
│   ├── partitions/
│   │   └── min_spiffs.csv         # Partition table: dual 1.9MB OTA slots, 192KB raw spool
│   ├── src/
│   │   └── main.cpp               # PlatformIO entrypoint (includes app_main.h)
│   ├── wifi/
│   │   └── wifi.ino               # Arduino IDE sketch (includes app_main.h)
│   └── include/                   # Shared modular C++ firmware engine
│       ├── app_config.h           # NVS Preferences persistence (SSIDs, URLs, hardware)
│       ├── app_main.h             # Master state machine: boot, portal, cycle, sleep
│       ├── app_state.h            # Data structures & in-memory cycle telemetry models
│       ├── api_client.h           # HTTP client for registration and telemetry POST
│       ├── bluetooth_scanner.h    # 2.4 GHz BLE & Classic scan with payload de-randomization
│       ├── board_pins.h           # Hardware profiles (T18, T8, DevKit V1) & BatteryReader
│       ├── captive_portal.h       # Soft-AP (192.168.4.1) setup web server
│       ├── channel_analyzer.h     # 13-channel promiscuous airtime & frame analyzer
│       ├── latency_prober.h       # Assoc, DHCP, DNS, HTTP probes & firewall audit
│       ├── led_controller.h       # Non-blocking LED state machine
│       ├── ntp_sync.h             # SNTP synchronization with UTC epoch clock
│       ├── ota_manager.h          # Background OTA updater & streaming binary client
│       ├── spool_manager.h        # Raw flash partition binary circular buffer
│       └── wifi_scanner.h         # 802.11 b/g/n spectrum survey engine
├── scripts/
│   └── build-docs.js              # Compiles frontend.ts and generates docs/index.html & docs/404.html
└── worker/
    ├── package.json               # Cloudflare Worker dependencies and scripts
    ├── tsconfig.json              # TypeScript compiler configuration
    ├── wrangler.toml              # Cloudflare Worker, D1 bindings, env vars & crons
    ├── migrations/                # D1 SQL migrations (0001 through 0010)
    └── src/
        ├── index.ts               # Worker router: routes requests to API, OTA, or Frontend
        ├── api.ts                 # REST API endpoints (/api/stations, /api/station/:id)
        ├── cron.ts                # Scheduled daily maintenance (00:15 UTC): rollups & pruning
        ├── db.ts                  # D1 Database client wrapper and SQL query engine
        ├── frontend.ts            # Single-source HTML/CSS/JS dashboard generator
        ├── ota.ts                 # OTA version evaluation and binary streaming proxy
        ├── register.ts            # One-time bootstrap registration & token generation
        ├── telemetry.ts           # Telemetry ingestion, deduplication, and cache invalidation
        └── types.ts               # TypeScript interfaces for API, D1, and ESP32 telemetry
```

---

## 3. Key Invariants & Non-Negotiable Rules

When modifying or refactoring this repository, agents **must** adhere to the following invariants:

### A. Dual Build Firmware Parity (PlatformIO & Arduino IDE)
* **Never duplicate code**: All embedded logic resides in `firmware/include/*.h`.
* `firmware/src/main.cpp` and `firmware/wifi/wifi.ino` are minimal wrappers that invoke `AppMain::setup()` and `AppMain::loop()`. Any changes must remain 100% compatible with both build systems.

### B. Flash Wear & Memory Safety
* **In-memory first**: ESP32 normal online cycles transmit telemetry directly from RAM without touching SPI flash memory.
* **18-Day Offline Ring Buffer**: Spooling uses a raw 192 KB binary partition (`raw_spool` defined in `min_spiffs.csv`) accessed via direct SPI flash block reads/writes (`esp_partition_read`/`write`). When offline, intermediate cycles accumulate in RAM; consolidated 30-minute average slots (~110-120 bytes) are written to flash, guaranteeing 18 to 31+ days of retention. Backlog is drained via chunked transactional uploads (`confirmBatchUploaded`). **Never** reintroduce LittleFS or SPIFFS for telemetry logs.
* **Zero-Redirect OTA Streaming**: The ESP32 `HTTPUpdate` client struggles with HTTP 302/307 redirects due to memory constraints during TLS re-negotiation. Binary downloads must be served as direct octet-streams via the Worker proxy route (`/api/ota/download/:filename`).

### C. Thread Safety & lwIP Sockets
* **POSIX BSD Sockets**: Arduino's `WiFiUdp` is **not** thread-safe and triggers lwIP core lock assertions when invoked concurrently with Wi-Fi operations. All low-level UDP/DNS probes in `latency_prober.h` must use standard POSIX BSD sockets (`sys/socket.h`, `netdb.h`).

### D. Single-Source Frontend & Critical Escaping Rule
* The dashboard HTML is generated by `renderDashboardHtml()` inside `worker/src/frontend.ts`.
* `scripts/build-docs.js` compiles `frontend.ts` and renders `docs/index.html` and `docs/404.html`.
* **CRITICAL TEMPLATE LITERAL RULE**: Because `renderDashboardHtml()` returns a TypeScript template string (`` `...` ``), any `${...}` in client-side JavaScript will be evaluated by the TypeScript compiler at compile-time and throw an error unless:
  1. It is escaped as `\${...}`, OR
  2. It is written using string concatenation (`'...' + val + '...'`).
* **Always** run `cd worker && npm run build:docs` after editing `frontend.ts`.

---

## 4. Hardware Profiles & ADC Battery Sensing

The firmware operates on a dynamic hardware profile model configured during captive portal setup and persisted in NVS:

| Board Target | Status LED Pin | ADC Battery Pin | Divider Ratio | Notes |
| ------------ | -------------- | --------------- | ------------- | ----- |
| **BOARD_T18** (LILYGO T18) | GPIO 5 | GPIO 35 | 0.50f (100k/100k) | Standard LiPo single-cell |
| **BOARD_T8** (LILYGO T8) | GPIO 21 | GPIO 35 | 0.50f (100k/100k) | Standard LiPo single-cell |
| **BOARD_DEVKIT** (DevKit V1) | GPIO 2 | -1 (Disabled) | 0.50f | AC / USB powered |
| **BOARD_C3_SUPERMINI** (ESP32-C3) | GPIO 8 (Active Low) | -1 (Disabled) | 0.50f | RISC-V Single-Core, USB Power Bank / 5V |
| **BOARD_CUSTOM** | User-defined | User-defined | User-defined | Custom GPIOs |

### Battery Voltage Calculations
* ADC voltage is read using `analogReadMilliVolts(pin)` across 5 averaged samples and divided by `hw.battery_divider`.
* Standard single-cell LiPo curve spans 3.30 V (0%) to 4.20 V (100%).
* Discharging detection: Identifies continuous voltage decreases $\ge 8\text{ mV}$ over $\ge 20\text{ minutes}$.
* Formatted strings required by UI:
  - `"Running on battery for XXXh (DD days X hours)"`
  - `"Estimated runtime YYYh (DD days Y hours)"`

---

## 5. Backend & Cloudflare D1 Management

### Two-Tier Caching Architecture
1. **Level 1 (Isolate In-Memory Cache)**: Fast global map in Worker memory (`maxAge: 15s`).
2. **Level 2 (D1 Snapshot Cache)**: `station_overview_cache` table (`maxAge: 600s`).
* When telemetry is uploaded (`telemetry.ts`), **both caches are explicitly invalidated** for that station so fresh data is visible immediately.

### D1 Migration Process
New migrations are added to `worker/migrations/` using zero-padded prefixes:
```bash
# Apply migrations locally
cd worker && npx wrangler d1 migrations apply wifi-mon-db --local

# Apply migrations remotely to production
cd worker && npx wrangler d1 migrations apply wifi-mon-db --remote
```

### Scheduled Maintenance (Cron)
A cron trigger runs daily at `00:15 UTC` (`cron.ts`):
1. Aggregates the previous UTC day's raw measurements into `daily_summaries`.
2. Purges raw `measurements`, `station_discovered_aps`, and `station_battery_history` older than 30 days.
3. Purges `station_overview_cache` older than 24 hours.

---

## 6. Version Synchronization Checklist

When releasing a new version, update versions across all components in lockstep:

```
┌─────────────────────────────────┬───────────────────────────────────────────┐
│ Component                       │ Files to Update                           │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 1. Firmware Version             │ firmware/platformio.ini                   │
│                                 │ firmware/src/main.cpp                     │
│                                 │ firmware/wifi/wifi.ino                    │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 2. Worker & Fleet OTA Target    │ worker/wrangler.toml (STABLE_VERSION)     │
│                                 │ worker/src/ota.ts (fallback version)      │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 3. Web UI & Documentation       │ worker/package.json (version)             │
│                                 │ worker/src/frontend.ts (brand tag & link) │
│                                 │ CHANGELOG.md (release entry)              │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 4. Build & Regenerate           │ cd worker && npm run build                │
│                                 │ cd worker && npm run build:docs           │
│                                 │ cd worker && npx wrangler deploy          │
└─────────────────────────────────┴───────────────────────────────────────────┘
```

---

## 7. Standard Verification & Testing Commands

Before committing changes, agents **must** run the following verification steps:

```bash
# 1. Validate TypeScript compiler on Worker backend
cd worker
npm run build

# 2. Recompile frontend and validate embedded JavaScript syntax
npm run build:docs

# 3. Check git status to ensure docs/index.html & docs/404.html are generated
git status

# 4. Optional: Deploy worker to production Cloudflare Edge (use --keep-vars to preserve dashboard secrets)
npx wrangler deploy --keep-vars
```

---

## 8. Admin & Fleet Management Architecture

### Authentication & Token Model (`/api/admin/*`)
- **Credentials**: Configured via Worker variables `ADMIN_USERNAME` and `ADMIN_PASSWORD` in Cloudflare Dashboard / `wrangler.toml`.
- **HMAC-SHA256 Tokens**: Bearer tokens are cryptographically generated using Web Crypto API (`crypto.subtle`) with payload `${username}:${expiry}` signed by `ADMIN_PASSWORD`. Valid for 7 days.
- **Timing-Safe Verification**: All string comparisons for usernames and passwords use constant-time byte XOR comparison (`timingSafeEqual`) to mitigate timing side-channel attacks.
- **Client Storage**: Browser stores the token in `localStorage.getItem("wifi_admin_token")` and attaches it via `Authorization: Bearer <token>`.
- **Deployment Safety**: When deploying via Wrangler, **always use `--keep-vars`** (`npx wrangler deploy --keep-vars`) to prevent overwriting dashboard-configured secrets and passwords with default repository values.

### Admin API Endpoints
| Endpoint | Method | Auth | Description |
|---|---|---|---|
| `/api/admin/login` | `POST` | Public | Validates credentials; returns HMAC session token |
| `/api/admin/verify` | `GET` | Bearer | Validates session token freshness and identity |
| `/api/admin/overview` | `GET` | Bearer | Returns D1 database sizing, row counts, fleet span, station stats, and yearly activity (`?year=YYYY&station_id=ID`) |
| `/api/admin/station/update` | `POST` | Bearer | Modifies station description, toggles Canary OTA track, or pins explicit target version |
| `/api/admin/station/delete` | `POST` | Bearer | Removes station registration with optional cascade purge (`purge_data: boolean`) |
| `/api/admin/data/delete` | `POST` | Bearer | Timeframe-based raw telemetry cleanup across `measurements`, `station_discovered_aps` (using `last_seen`), `station_bluetooth_summary`, `station_channel_utilization`, and `station_battery_history`. Preserves daily rollups. |

---

## 9. Frontend Design Decisions & Replicability Guidelines

### CSS Namespace Separation for Heatmaps
To avoid style collisions between the two distinct heatmap components:
1. **24-Hour Spectrum & BLE Heatmap**:
   - Selector: `.heatmap-cell`
   - Properties: `height: 18px; min-width: 13px; flex: 1; border-radius: 2px;` with `gap: 2px;` and **no borders**.
   - Hover: `transform: scale(1.25); outline: 1.5px solid #fff;`.
2. **Year Activity Calendar Heatmap (Settings)**:
   - Namespace: `.cal-*` (`.cal-card`, `.cal-scroll-wrap`, `.cal-grid-wrap`, `.cal-months-row`, `.cal-days-row`, `.cal-weekday-label`, `.cal-cell`, `.cal-legend`, `.cal-legend-box`).
   - Modal window width: `.modal-window.large` is sized to `max-width: 1040px;` to fit the 53-column week grid (876px + padding) without horizontal scrollbar overflow.
   - Legend boxes: Strict `11px × 11px` squares with dynamic dataset-driven descriptions (`0 cycles / day` to `${maxCount} cycles / day (peak)`).

### 24-Hour Station Reliability Bar (48 × 30-Minute Interval Matrix)
- Placed directly inside each observed network card below the HTTP Probe Latencies section.
- Partitions the last 24 hours into 48 discrete 30-minute intervals (`t0 = now - 48 * 1800`).
- **Hierarchical Failure Classification**:
  1. `Association Failure`: `m.assoc_ms === -1` or status contains `assoc` → **Charcoal / Dark Black** (`#0f172a`, border `#475569`).
  2. `DHCP Lease Timeout`: `m.dhcp_ms === -1` or status contains `dhcp` → **Red** (`#ef4444`).
  3. `Gateway RTT Timeout`: `m.gateway_ms === -1` → **Orange** (`#f97316`).
  4. `DNS v4/v6 Timeout`: `m.dns_ipv4_ms === -1` or `m.dns_ipv6_ms === -1` → **Amber** (`#f59e0b`).
  5. `HTTP Probe Timeout`: Any of `cloudflare_ms`, `google_ms`, `microsoft_ms`, `facebook_ms`, `netflix_ms` is `-1` → **Purple** (`#8b5cf6`).
  6. `Healthy (100% Passed)`: All probes $\ge 0$ → **Green** (`#10b981`).
  7. `Idle / No Run`: No sample in interval → **Muted Gray** (`rgba(148, 163, 184, 0.15)`).

### Collapsible Dashboard Sections
- "Surrounding 2.4 GHz Spectrum Survey" and "Previously Detected Networks" sections use interactive headers with collapse icons (`▶` / `▼`).
- Drawers (`#surveyTableDrawer` and `#inactiveSurveyDrawer`) are `display: none` by default to preserve vertical screen space on boot.

### D1 Database Storage Estimation (No `dbstat` Support)
- Cloudflare D1 blocks direct SQLite `dbstat` virtual table queries and `PRAGMA page_count` via API (throws `SQLITE_AUTH` or `no such table`).
- Sizing is computed via parallel `db.batch()` counting across all 13 tables (~1.4 ms), multiplied by schema structural byte estimates (row byte averages plus B-tree index overhead), producing exact live percentages and formatted KB/MB totals.
```
