# Power Consumption Analysis & Energy Optimization Playbook

This document details the energy consumption profile, mathematical power modeling, hardware battery runtime estimates, and power reduction strategies for the **T18 Network Reliability Monitor** across measured hardware targets:
- **LILYGO TTGO T18** (ESP32 on 18650 LiPo battery via onboard TP5400)
- **ESP32 DevKit V1** (ESP32 with onboard USB-UART and AMS1117 LDO)
- **ESP32-C3 SuperMini** (RISC-V with native USB and low-quiescent LDO)

---

## 1. Measured Operating States & Current Draw Specifications

The power model is calibrated with measured light sleep quiescent currents and RF active states:

| Operating State | ESP32 T18 (LiPo) | ESP32 DevKit V1 | ESP32-C3 SuperMini | Operational Details |
|---|---|---|---|---|
| **Light Sleep ($I_{\text{sleep}}$)** | **2.5 mA** | **17.0 mA** | **1.0 mA** | Radio OFF, CPU clock-gated in `esp_light_sleep_start()`. T18 benefits from direct LiPo path; DevKit has CP2102 + AMS1117 load; C3 has native USB & modern low-leakage core. |
| **Wi-Fi Transmission ($I_{\text{tx}}$)** | **120.0 mA** | **120.0 mA** | **84.0 mA** | Active 802.11b/g/n transmission during NTP sync, latency probes (HTTP GET), and Worker telemetry upload. |
| **Wi-Fi Spectrum Scan ($I_{\text{scan}}$)** | **120.0 mA** | **120.0 mA** | **84.0 mA** | Promiscuous 13-channel congestion survey and active SSID discovery scan. |
| **Bluetooth Sweep ($I_{\text{bt}}$)** | **110.0 mA** | **110.0 mA** | **84.0 mA** | BLE scanning (3.0s) + Classic BT inquiry (2.0s on ESP32; omitted on RISC-V C3). |
| **Power Bank Keep-Alive Burst ($I_{\text{burst}}$)** | N/A *(LiPo)* | N/A *(Mains/USB)* | **84.0 mA** | Active RF probe pulse (120 ms every 15 s) to prevent external USB power banks from shutting down. |

---

## 2. Power Bank Keep-Alive Burst Modeling (ESP32-C3)

Modern USB power banks automatically shut off when load current drops below $\approx 30\text{--}50\text{ mA}$ for more than 15–30 seconds. Because the ESP32-C3 draws only **1.0 mA** in light sleep, the firmware injects an active probe pulse:

- **Pulse Duration ($t_{\text{pulse}}$)**: $120\text{ ms} = 0.12\text{ s}$
- **Pulse Interval ($T_{\text{pulse}}$)**: $15.0\text{ s}$
- **Duty Cycle ($\delta_{\text{burst}}$)**:
  $$\delta_{\text{burst}} = \frac{0.12\text{ s}}{15.0\text{ s}} = 0.008\quad (0.80\%)$$

### Effective Sleep Current ($I_{\text{sleep, eff}}$)
During each 15-second interval:
$$I_{\text{sleep, eff}} = \frac{(T_{\text{pulse}} - t_{\text{pulse}}) \times I_{\text{sleep}} + t_{\text{pulse}} \times I_{\text{burst}}}{T_{\text{pulse}}}$$
$$I_{\text{sleep, eff}} = \frac{14.88\text{ s} \times 1.0\text{ mA} + 0.12\text{ s} \times 84.0\text{ mA}}{15.0\text{ s}} = \frac{14.88 + 10.08}{15.0} = \frac{24.96}{15.0} = \mathbf{1.664\text{ mA}}$$

> **Impact**: The keep-alive burst increases effective sleep draw from $1.0\text{ mA}$ to only **$1.664\text{ mA}$** ($+0.664\text{ mA}$), perfectly sustaining the power bank without significant battery penalty.

---

## 3. Daily Cycle Timing Breakdown (10-Minute Cadence)

Every 24 hours contains $24 \times 6 = 144$ ten-minute cycles partitioned into:
1. **Standard Cycles ($96\times$/day)**: Wi-Fi scan, BLE scan, and Worker upload.
2. **30-Minute Cadence Cycles ($48\times$/day)**: Full 13-channel promiscuous congestion survey, Wi-Fi scan, BLE scan, latency probes, and Worker upload.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        24-Hour Cycle Schedule                          │
│                                                                        │
│   ┌────────────────────┐ ┌────────────────────┐ ┌────────────────────┐  │
│   │  Cycle 1 (10 min)  │ │  Cycle 2 (20 min)  │ │  Cycle 3 (30 min)  │  │
│   │  Type A: Standard  │ │  Type A: Standard  │ │  Type B: Deep Scan │  │
│   │  (Active: 8.5-10s) │ │  (Active: 8.5-10s) │ │  (Active: 38-40s)  │  │
│   └────────────────────┘ └────────────────────┘ └────────────────────┘  │
│          ▲                      ▲                      ▲               │
│          └──────────────────────┴──────────────────────┘               │
│                 Repeats 48 times per day = 144 cycles/day              │
└────────────────────────────────────────────────────────────────────────┘
```

### Active Phase Durations

| Task / Activity | Type A: Standard (96/day) | Type B: 30-Min Boundary (48/day) |
|---|---|---|
| **13-Channel Survey** ($13 \times 2.0\text{s}$) | 0.0 s (Skipped) | 26.0 s |
| **Wi-Fi AP Scan** (`scanNetworks`) | 3.0 s | 3.0 s |
| **Bluetooth Scan** (BLE + Classic) | 5.0 s (ESP32) / 3.0 s (C3) | 5.0 s (ESP32) / 3.0 s (C3) |
| **Network Latency Probes** (Assoc, DHCP, HTTP) | 0.0 s (Skipped) | 3.0 s |
| **Worker Upload & NTP Sync** | 2.5 s | 3.0 s |
| **Total Active Time ($t_{\text{active}}$)** | **10.5 s (ESP32) / 8.5 s (C3)** | **40.0 s (ESP32) / 38.0 s (C3)** |
| **Sleep Time ($t_{\text{sleep}}$)** | **589.5 s (ESP32) / 591.5 s (C3)** | **560.0 s (ESP32) / 562.0 s (C3)** |

---

## 4. Daily Power Consumption Calculations (mAh / Day)

### Target 1: ESP32 T18 on LiPo Battery ($I_{\text{sleep}} = 2.5\text{ mA}$)

#### 1. Daily Active Energy ($I_{\text{active}} \approx 120.0\text{ mA}$)
- $T_{\text{active, A}} = 96 \times 10.5\text{ s} = 1,008\text{ s}$
- $T_{\text{active, B}} = 48 \times 40.0\text{ s} = 1,920\text{ s}$
- $T_{\text{active, total}} = 1,008 + 1,920 = 2,928\text{ s} = 0.8133\text{ hours}$
$$Q_{\text{active}} = 0.8133\text{ h} \times 120.0\text{ mA} = \mathbf{97.60\text{ mAh}}$$

#### 2. Daily Sleep Energy ($I_{\text{sleep}} = 2.5\text{ mA}$)
- $T_{\text{sleep, total}} = 86,400\text{ s} - 2,928\text{ s} = 83,472\text{ s} = 23.1867\text{ hours}$
$$Q_{\text{sleep}} = 23.1867\text{ h} \times 2.5\text{ mA} = \mathbf{57.97\text{ mAh}}$$

#### 3. Total Daily Consumption
$$Q_{\text{daily, T18}} = 97.60 + 57.97 = \mathbf{155.57\text{ mAh / day}}$$
$$\bar{I}_{\text{T18}} = \frac{155.57\text{ mAh}}{24.0\text{ h}} = \mathbf{6.48\text{ mA average}}$$

---

### Target 2: ESP32 DevKit V1 ($I_{\text{sleep}} = 17.0\text{ mA}$)

#### 1. Daily Active Energy ($I_{\text{active}} \approx 120.0\text{ mA}$)
$$Q_{\text{active}} = 0.8133\text{ h} \times 120.0\text{ mA} = \mathbf{97.60\text{ mAh}}$$

#### 2. Daily Sleep Energy ($I_{\text{sleep}} = 17.0\text{ mA}$)
$$Q_{\text{sleep}} = 23.1867\text{ h} \times 17.0\text{ mA} = \mathbf{394.17\text{ mAh}}$$

#### 3. Total Daily Consumption
$$Q_{\text{daily, DevKit}} = 97.60 + 394.17 = \mathbf{491.77\text{ mAh / day}}$$
$$\bar{I}_{\text{DevKit}} = \frac{491.77\text{ mAh}}{24.0\text{ h}} = \mathbf{20.49\text{ mA average}}$$

---

### Target 3: ESP32-C3 on 10-Minute Cadence (+ Keep-Alive Burst, $I_{\text{sleep, eff}} = 1.664\text{ mA}$)

#### 1. Daily Active Energy ($I_{\text{active}} \approx 84.0\text{ mA}$)
- $T_{\text{active, A}} = 96 \times 8.5\text{ s} = 816\text{ s}$
- $T_{\text{active, B}} = 48 \times 38.0\text{ s} = 1,824\text{ s}$
- $T_{\text{active, total}} = 816 + 1,824 = 2,640\text{ s} = 0.7333\text{ hours}$
$$Q_{\text{active}} = 0.7333\text{ h} \times 84.0\text{ mA} = \mathbf{61.60\text{ mAh}}$$

#### 2. Daily Sleep Energy ($I_{\text{sleep, eff}} = 1.664\text{ mA}$)
- $T_{\text{sleep, total}} = 86,400\text{ s} - 2,640\text{ s} = 83,760\text{ s} = 23.2667\text{ hours}$
$$Q_{\text{sleep}} = 23.2667\text{ h} \times 1.664\text{ mA} = \mathbf{38.72\text{ mAh}}$$

#### 3. Total Daily Consumption
$$Q_{\text{daily, C3-10m}} = 61.60 + 38.72 = \mathbf{100.32\text{ mAh / day}}$$
$$\bar{I}_{\text{C3-10m}} = \frac{100.32\text{ mAh}}{24.0\text{ h}} = \mathbf{4.18\text{ mA average}}$$

---

### Target 4: ESP32-C3 in Default Powered Mode (2-Minute Intervals)
- $720\text{ cycles/day}$, Active time: $6.640\text{ hours/day}$ ($27.67\%$ duty cycle)
$$Q_{\text{active}} = 6.640\text{ h} \times 84.0\text{ mA} = 557.76\text{ mAh}$$
$$Q_{\text{sleep}} = 17.360\text{ h} \times 1.664\text{ mA} = 28.89\text{ mAh}$$
$$Q_{\text{daily, C3-2m}} = \mathbf{586.65\text{ mAh / day}}\quad (\bar{I} = \mathbf{24.44\text{ mA average}})$$

---

## 5. Hardware Runtime Estimates

### 1. ESP32 T18 on 2000 mAh LiPo Battery (Single 1S Cell)
- **Battery**: $2000\text{ mAh}$ single-cell LiPo ($3.7\text{ V}$ nominal, $3.30\text{ V}$ cutoff).
$$\text{Runtime}_{\text{T18 (2.5mA)}} = \frac{2000\text{ mAh}}{6.48\text{ mA}} = \mathbf{308.64\text{ hours}}\quad (\mathbf{12.86\text{ days}})$$

- **With 800ms Channel Dwell Optimization**:
  Active time drops by $15.6\text{ s}$ per 30 minutes ($Q_{\text{active}} = 72.64\text{ mAh}$, $Q_{\text{sleep}} = 58.49\text{ mAh}$, total = $131.13\text{ mAh/day}$, $\bar{I} = 5.46\text{ mA}$):
  $$\text{Runtime}_{\text{T18 (optimized)}} = \frac{2000\text{ mAh}}{5.46\text{ mA}} = \mathbf{366.30\text{ hours}}\quad (\mathbf{15.26\text{ days}})$$

---

### 2. ESP32 DevKit V1 on 2000 mAh LiPo Battery (Reference)
$$\text{Runtime}_{\text{DevKit (17mA)}} = \frac{2000\text{ mAh}}{20.49\text{ mA}} = \mathbf{97.61\text{ hours}}\quad (\mathbf{4.07\text{ days}})$$

---

### 3. ESP32-C3 on 10,000 mAh 5V USB Power Bank
- **Internal Energy**: $10,000\text{ mAh} \times 3.7\text{ V} = 37.0\text{ Wh}$
- **DC-DC Step-Up Boost Efficiency**: $\eta \approx 85\%$
- **Usable 5V Capacity**:
  $$C_{\text{eff, 5V}} = \frac{37.0\text{ Wh} \times 0.85}{5.0\text{ V}} = \mathbf{6,290\text{ mAh}}$$

#### On 10-Minute Cadence (+ Keep-Alive Burst):
$$\text{Runtime}_{\text{C3 (10m)}} = \frac{6,290\text{ mAh}}{4.18\text{ mA}} = \mathbf{1,504.78\text{ hours}}\quad (\mathbf{62.70\text{ days}}\text{ / over 2 months!})$$
*(Referenced against raw nominal $10,000\text{ mAh} @ 3.7\text{V}$: $\mathbf{2,392\text{ hours}} \approx \mathbf{99.7\text{ days}}$)*

#### In Default 2-Minute Mode:
$$\text{Runtime}_{\text{C3 (2m)}} = \frac{6,290\text{ mAh}}{24.44\text{ mA}} = \mathbf{257.36\text{ hours}}\quad (\mathbf{10.72\text{ days}})$$

#### With 800ms Channel Dwell Optimization (10-Minute Cadence):
$$\bar{I} = 3.47\text{ mA} \implies \text{Runtime} = \frac{6,290\text{ mAh}}{3.47\text{ mA}} = \mathbf{1,812.68\text{ hours}}\quad (\mathbf{75.53\text{ days}}\text{ / 2.5 months!})$$

---

## 6. Alignment with the 18-Day Offline Ring Buffer

The updated calculations highlight how well the hardware runtimes align with the **18-day offline ring buffer** (864 slots $\times$ 30 minutes = 432 hours):

1. **ESP32 T18 on 2000 mAh LiPo**: Lasts **12.9 to 15.3 days**, utilizing $\approx 75\text{--}85\%$ of the 18-day ring buffer capacity before battery depletion.
2. **ESP32-C3 on 10,000 mAh Power Bank**: Lasts **62.7 days**, easily surviving multi-week network blackouts while retaining full 18-day rolling history in flash.

---

## 7. Master Comparison Matrix

| Configuration | Sleep Current | Daily Consumption | Average Current | 2000 mAh LiPo | 10,000 mAh 5V Power Bank |
|---|---|---|---|---|---|
| **ESP32 DevKit V1 (10m)** | 17.0 mA | 491.8 mAh | 20.5 mA | **4.07 days (97.6h)** | 12.8 days (307h) |
| **ESP32 T18 (Current 10m)** | 2.5 mA | 155.6 mAh | 6.48 mA | **12.86 days (308.6h)** | 40.4 days (971h) |
| **ESP32 T18 (Optimized 800ms)** | 2.5 mA | 131.1 mAh | 5.46 mA | **15.26 days (366.3h)** | 48.0 days (1152h) |
| **ESP32-C3 (Default 2m)** | 1.66 mA eff | 586.7 mAh | 24.44 mA | 3.41 days (81.8h) | **10.72 days (257h)** |
| **ESP32-C3 (10m + Keep-Alive)** | 1.66 mA eff | 100.3 mAh | 4.18 mA | 19.9 days (478h) | **62.70 days (1505h)** |
| **ESP32-C3 (Optimized 800ms)** | 1.66 mA eff | 83.2 mAh | 3.47 mA | 24.0 days (577h) | **75.53 days (1813h)** |
