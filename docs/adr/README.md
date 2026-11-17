# Architecture Decision Records (ADRs)

This directory documents the foundational architectural and design decisions governing `kreier/wifi`.

## Index of Records

| ADR | Title | Status | Date | Summary |
| --- | ----- | ------ | ---- | ------- |
| [0001](0001-d1-over-git-storage.md) | Use Cloudflare D1 Over Direct Git-Based Storage for Telemetry | Accepted | 2026-11-13 | Serverless edge SQLite with 30-day retention and automated daily cron rollups, replacing git commit spam and rate limiting. |
| [0002](0002-per-device-token-provisioning.md) | Zero-Trust Per-Device Token Provisioning via Bootstrap Secret | Accepted | 2026-11-13 | One-time bootstrap secret in captive portal exchanged for unique per-device UUID Bearer token stored in NVS; master phrase erased. |
| [0003](0003-dual-build-layout.md) | Dual-Compatible Firmware Build Layout (PlatformIO & Arduino IDE) | Accepted | 2026-11-13 | Single-source C++ modular architecture in `firmware/include/` unified across PlatformIO CI/CD and Arduino IDE desktop flashing. |
| [0004](0004-fleet-ota-and-battery-monitoring.md) | Automated Fleet OTA Updates, Action Runner Modernization, and Battery Telemetry History | Accepted | 2026-11-15 | Edge OTA dispatch with streaming proxies, transient socket retry backoff, 30-day battery discharge modeling, and Node 24 CI runners. |
| [0005](0005-bluetooth-ble-spectrum-and-derandomization.md) | 2.4 GHz Bluetooth Classic & BLE Ingestion with Advertising Payload De-Randomization | Accepted | 2026-11-16 | Dual BLE & Classic scanning, advertising payload fingerprinting to defeat MAC randomization, D1 telemetry tables, and web dashboard visualization. |
| [0006](0006-power-aware-cadence-and-30min-aggregation.md) | Power-Aware Measurement Cadence, 30-Minute Rollup Aggregation, and Zero-Flash-Wear Invariants | Accepted | 2026-11-17 | Dual battery (10m/30m) vs powered (2m/10m) measurement cadence, zero flash wear when online, D1 daily 30-min rollup aggregations, and change-only diagnostics history. |
