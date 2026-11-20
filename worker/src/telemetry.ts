import { Env, TelemetryCyclePayload, MeasurementRecord, ScanSurveyRecord } from "./types";
import { Database } from "./db";
import { invalidateMemoryCache } from "./api";

export async function handleTelemetry(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response(JSON.stringify({ error: "Method not allowed" }), {
      status: 405,
      headers: { "Content-Type": "application/json" }
    });
  }

  // 1. Authenticate Bearer <device_token>
  const authHeader = request.headers.get("Authorization");
  if (!authHeader || !authHeader.startsWith("Bearer ")) {
    return new Response(JSON.stringify({ error: "Missing or malformed Authorization header" }), {
      status: 401,
      headers: { "Content-Type": "application/json" }
    });
  }

  const token = authHeader.substring(7).trim();
  const db = new Database(env);
  const station = await db.getStationByToken(token);

  if (!station) {
    return new Response(JSON.stringify({ error: "Invalid device token" }), {
      status: 401,
      headers: { "Content-Type": "application/json" }
    });
  }

  // 2. Parse payload: could be a single TelemetryCyclePayload or an array of cycles
  let rawBody: unknown;
  try {
    rawBody = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const cycles: TelemetryCyclePayload[] = Array.isArray(rawBody) ? rawBody : [rawBody as TelemetryCyclePayload];

  // Auto-sync station name & description if changed in captive portal
  for (const c of cycles) {
    if (c.station_id && c.station_id.trim() && c.station_id.trim() !== station.station_id) {
      const newStationId = c.station_id.trim();
      console.log(`[Telemetry] Station rename detected from '${station.station_id}' to '${newStationId}'`);
      await db.renameStation(station.station_id, newStationId);
      station.station_id = newStationId;
    }
    if (c.description && c.description.trim() && c.description.trim() !== station.description) {
      const newDesc = c.description.trim();
      console.log(`[Telemetry] Station description update detected for '${station.station_id}': '${newDesc}'`);
      await db.updateStationDescription(station.station_id, newDesc);
      station.description = newDesc;
    }
  }

  const measurementsToInsert: MeasurementRecord[] = [];
  const surveysToInsert: ScanSurveyRecord[] = [];
  const batteryRecordsToInsert: { station_id: string; timestamp: number; battery_mv: number }[] = [];
  const serverNow = Math.floor(Date.now() / 1000);
  let latestTimestamp = station.last_seen || 0;
  let latestScanDuration: number | null = null;
  let insertedChannels = 0;

  for (const cycle of cycles) {
    let cycleTime = cycle.timestamp || serverNow;
    // Guard against invalid pre-2024 timestamps (IoT probe without NTP sync, local uptime ticks)
    // or future timestamps caused by NTP / clock skew
    if (cycleTime < 1700000000 || cycleTime > serverNow + 30) {
      cycleTime = serverNow;
    }
    if (cycleTime > latestTimestamp) {
      latestTimestamp = cycleTime;
    }

    const scanDuration = typeof cycle.scan_duration_ms === "number" ? cycle.scan_duration_ms : null;
    if (scanDuration !== null && scanDuration < 15000) {
      latestScanDuration = scanDuration;
    }

    const batMv = typeof cycle.battery_mv === "number" && cycle.battery_mv >= 2500 && cycle.battery_mv <= 5000 ? cycle.battery_mv : null;
    if (batMv !== null) {
      batteryRecordsToInsert.push({
        station_id: station.station_id,
        timestamp: cycleTime,
        battery_mv: batMv
      });
    }

    // Process network latency measurements
    if (Array.isArray(cycle.networks)) {
      for (const net of cycle.networks) {
        if (!net.ssid) continue;
        measurementsToInsert.push({
          station_id: station.station_id,
          timestamp: cycleTime,
          ssid: net.ssid,
          bssid: net.bssid ? net.bssid.trim().toUpperCase() : null,
          channel: typeof net.channel === "number" ? net.channel : null,
          assoc_ms: typeof net.assoc_ms === "number" ? net.assoc_ms : null,
          dhcp_ms: typeof net.dhcp_ms === "number" ? net.dhcp_ms : null,
          google_ms: typeof net.http_latencies?.google === "number" ? net.http_latencies.google : null,
          cloudflare_ms: typeof net.http_latencies?.cloudflare === "number" ? net.http_latencies.cloudflare : null,
          microsoft_ms: typeof net.http_latencies?.microsoft === "number" ? net.http_latencies.microsoft : null,
          facebook_ms: typeof net.http_latencies?.facebook === "number" ? net.http_latencies.facebook : null,
          netflix_ms: typeof net.http_latencies?.netflix === "number" ? net.http_latencies.netflix : null,
          apple_ms: typeof net.http_latencies?.apple === "number" ? net.http_latencies.apple : null,
          gateway_ms: typeof net.gateway_ms === "number" ? net.gateway_ms : null,
          dns_ipv4_ms: typeof net.dns_ipv4_ms === "number" ? net.dns_ipv4_ms : null,
          dns_ipv6_ms: typeof net.dns_ipv6_ms === "number" ? net.dns_ipv6_ms : null,
          scan_duration_ms: scanDuration,
          battery_mv: batMv,
          status: net.status || "connected"
        });
      }
    }

    // Process daily network diagnostics if present in this cycle
    if (cycle.diagnostics) {
      if (Array.isArray(cycle.diagnostics)) {
        for (const d of cycle.diagnostics) {
          await db.saveNetworkDiagnostics(station.station_id, cycleTime, d);
        }
      } else {
        await db.saveNetworkDiagnostics(station.station_id, cycleTime, cycle.diagnostics);
      }
    }

    // Process 30-minute promiscuous channel utilization metrics if present
    if (Array.isArray(cycle.channel_utilization) && cycle.channel_utilization.length > 0) {
      insertedChannels += await db.saveChannelUtilization(
        station.station_id,
        cycleTime,
        scanDuration,
        cycle.channel_utilization
      );
    }

    // Process Bluetooth BLE and Classic telemetry if present
    if (cycle.bluetooth) {
      await db.saveBluetoothTelemetry(station.station_id, cycleTime, cycle.bluetooth);
    }

    // Real-time indexing into daily_30min_summaries for instantaneous 7-day and 24h heatmap rendering
    if ((Array.isArray(cycle.channel_utilization) && cycle.channel_utilization.length > 0) || cycle.bluetooth) {
      let chanAirtimeArr: number[] | null = null;
      if (Array.isArray(cycle.channel_utilization) && cycle.channel_utilization.length > 0) {
        chanAirtimeArr = Array(13).fill(0);
        for (const cu of cycle.channel_utilization) {
          if (cu.channel >= 1 && cu.channel <= 13) {
            chanAirtimeArr[cu.channel - 1] = Math.round(cu.airtime_pct);
          }
        }
      }
      const bleMax = cycle.bluetooth?.ble_unique_devices ?? 0;
      const classicMax = cycle.bluetooth?.classic_total ?? 0;
      await db.upsertDaily30MinSummary(
        station.station_id,
        cycleTime,
        chanAirtimeArr ? JSON.stringify(chanAirtimeArr) : null,
        bleMax,
        classicMax,
        batMv
      );
    }

    // Process scan surveys
    if (Array.isArray(cycle.scan_survey)) {
      for (const item of cycle.scan_survey) {
        surveysToInsert.push({
          station_id: station.station_id,
          timestamp: cycleTime,
          ssid: item.ssid || "<Hidden>",
          bssid: item.bssid ? item.bssid.trim().toUpperCase() : null,
          rssi: typeof item.rssi === "number" ? item.rssi : null,
          quality: typeof item.quality === "number" ? item.quality : null,
          encryption: item.encryption || "Unknown",
          phy_mode: item.phy_mode || "WIFI_4",
          channel: typeof item.channel === "number" ? item.channel : null,
          band: item.band || "2.4 GHz"
        });
      }
    }
  }

  // 3. Batch insert into D1 with optimized storage
  const insertedM = await db.insertMeasurements(measurementsToInsert);
  const insertedS = await db.saveSurveys(station.station_id, latestTimestamp, surveysToInsert, latestScanDuration);
  if (batteryRecordsToInsert.length > 0) {
    await db.saveBatteryHistory(batteryRecordsToInsert);
  }

  // Extract latest hardware & firmware metadata if present
  let latestBattery: number | undefined;
  let latestBoard: string | undefined;
  let latestFw: string | undefined;
  for (const c of cycles) {
    if (typeof c.battery_mv === "number" && c.battery_mv >= 2500 && c.battery_mv <= 5000) {
      latestBattery = c.battery_mv;
    }
    if (c.board_type) latestBoard = c.board_type;
    if (c.firmware_version) latestFw = c.firmware_version;
  }

  await db.updateStationLastSeen(station.station_id, serverNow, {
    battery_mv: latestBattery,
    board_type: latestBoard,
    firmware_version: latestFw
  });

  // 4. Invalidate overview caches so fresh data is visible immediately
  invalidateMemoryCache(station.station_id);
  await db.invalidateOverviewCache(station.station_id);

  return new Response(
    JSON.stringify({
      success: true,
      station_id: station.station_id,
      inserted_measurements: insertedM,
      inserted_surveys: insertedS,
      inserted_channels: insertedChannels,
      cycles_processed: cycles.length
    }),
    {
      status: 200,
      headers: { "Content-Type": "application/json" }
    }
  );
}
