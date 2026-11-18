import { Env } from "./types";
import { Database } from "./db";

// In-memory cache for ultra-fast isolate hits (< 1ms, 0 D1 reads)
const ISOLATE_CACHE = new Map<string, { cachedAt: number; data: string }>();
let STATIONS_CACHE: { cachedAt: number; data: string } | null = null;
const CACHE_TTL_SEC = 600; // 10 minutes temporary dataset cache (matching 10-minute reporting interval)
const STATIONS_CACHE_TTL_SEC = 60; // 1 minute cache for station list

export function invalidateMemoryCache(stationId?: string) {
  if (stationId) {
    ISOLATE_CACHE.delete(stationId);
  } else {
    ISOLATE_CACHE.clear();
  }
  STATIONS_CACHE = null;
}

export async function handleApiStations(_request: Request, env: Env): Promise<Response> {
  const now = Math.floor(Date.now() / 1000);

  if (STATIONS_CACHE && (now - STATIONS_CACHE.cachedAt) < STATIONS_CACHE_TTL_SEC) {
    return new Response(STATIONS_CACHE.data, {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "X-Cache": "HIT-ISOLATE",
        "Cache-Control": "public, max-age=60"
      }
    });
  }

  const db = new Database(env);
  const stations = await db.listStations();
  const payload = JSON.stringify({ stations });

  STATIONS_CACHE = { cachedAt: now, data: payload };

  return new Response(payload, {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
      "X-Cache": "MISS",
      "Cache-Control": "public, max-age=60"
    }
  });
}

export async function handleApiStationDetail(stationId: string, _request: Request, env: Env): Promise<Response> {
  const now = Math.floor(Date.now() / 1000);

  // 1. Level 1 Cache: In-memory Map in Worker isolate (0 D1 reads, < 1ms)
  const memCached = ISOLATE_CACHE.get(stationId);
  if (memCached && (now - memCached.cachedAt) < CACHE_TTL_SEC) {
    return new Response(memCached.data, {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "X-Cache": "HIT-ISOLATE",
        "Cache-Control": "public, max-age=15, must-revalidate"
      }
    });
  }

  const db = new Database(env);

  // 2. Level 2 Cache: D1 temporary dataset table (costs only 1 row read on cold isolate!)
  const d1Cached = await db.getOverviewCache(stationId, CACHE_TTL_SEC);
  if (d1Cached) {
    ISOLATE_CACHE.set(stationId, { cachedAt: now, data: d1Cached });
    return new Response(d1Cached, {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "X-Cache": "HIT-D1",
        "Cache-Control": "public, max-age=15, must-revalidate"
      }
    });
  }

  // 3. Cache Miss: Query raw data and generate overview
  const station = await db.getStationById(stationId);

  if (!station) {
    return new Response(JSON.stringify({ error: "Station not found" }), {
      status: 404,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*"
      }
    });
  }

  // Remove secret token before returning public metadata
  const publicMeta = {
    station_id: station.station_id,
    description: station.description,
    created_at: station.created_at,
    last_seen: station.last_seen,
    firmware_version: station.firmware_version ?? null,
    board_type: station.board_type ?? null,
    battery_mv: station.battery_mv ?? null
  };

  const latestMeasurements = await db.getLatestMeasurements(stationId);
  const latestSurvey = await db.getLatestSurvey(stationId);
  const inactiveSurvey = await db.getInactiveSurveys(stationId);
  const recentMeasurements = await db.getRecentMeasurements(stationId, 1000); // 24-48h across all monitored SSIDs (144 samples/day per SSID)
  const dailySummaries = await db.getDailySummaries(stationId, 30);
  const diagnostics = await db.getLatestDiagnostics(stationId);
  const channelUtilization = await db.getRecentChannelUtilization(stationId, 168); // 7-day spectrum congestion history
  const latestChannelUtilization = await db.getLatestChannelUtilization(stationId);
  const latestScanDuration = await db.getLatestScanDuration(stationId);
  const thirtyDaysAgo = now - (30 * 86400);
  const batteryHistory = await db.getBatteryHistory(stationId, thirtyDaysAgo);
  const latestBluetooth = await db.getLatestBluetooth(stationId);
  const bluetoothHistory = await db.getRecentBluetooth(stationId, 168); // 7-day Bluetooth history
  const recentBluetoothDevices = await db.getRecentBluetoothDevices(stationId, 24);
  const daily30MinSummaries = await db.getDaily30MinSummaries(stationId, 7);

  const overviewPayload = JSON.stringify({
    station: publicMeta,
    latest_measurements: latestMeasurements,
    latest_survey: latestSurvey,
    inactive_survey: inactiveSurvey,
    recent_history: recentMeasurements,
    daily_summaries: dailySummaries,
    daily_30min_summaries: daily30MinSummaries,
    diagnostics: diagnostics,
    channel_utilization: channelUtilization,
    latest_channel_utilization: latestChannelUtilization,
    latest_scan_duration_ms: latestScanDuration,
    battery_history: batteryHistory,
    latest_bluetooth: latestBluetooth,
    bluetooth_history: bluetoothHistory,
    inactive_bluetooth: recentBluetoothDevices,
    recent_bluetooth_devices: recentBluetoothDevices
  });

  // Store in Level 1 & Level 2 cache
  ISOLATE_CACHE.set(stationId, { cachedAt: now, data: overviewPayload });
  await db.setOverviewCache(stationId, overviewPayload);

  return new Response(overviewPayload, {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
      "X-Cache": "MISS",
      "Cache-Control": "public, max-age=15, must-revalidate"
    }
  });
}

export async function handleApiExport(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const stationId = url.searchParams.get("station_id");
  const format = url.searchParams.get("format") || "json";

  const db = new Database(env);
  if (!stationId) {
    return new Response(JSON.stringify({ error: "station_id parameter required" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const measurements = await db.getRecentMeasurements(stationId, 1000);

  if (format === "csv") {
    let csv = "timestamp,ssid,assoc_ms,dhcp_ms,cloudflare_ms,google_ms,microsoft_ms,facebook_ms,netflix_ms,status\n";
    for (const m of measurements) {
      csv += `${m.timestamp},"${m.ssid}",${m.assoc_ms ?? ""},${m.dhcp_ms ?? ""},${m.cloudflare_ms ?? ""},${m.google_ms ?? ""},${m.microsoft_ms ?? ""},${m.facebook_ms ?? ""},${m.netflix_ms ?? ""},"${m.status ?? ""}"\n`;
    }
    return new Response(csv, {
      status: 200,
      headers: {
        "Content-Type": "text/csv",
        "Content-Disposition": `attachment; filename="${stationId}-telemetry.csv"`
      }
    });
  }

  return new Response(JSON.stringify(measurements, null, 2), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Content-Disposition": `attachment; filename="${stationId}-telemetry.json"`
    }
  });
}
