import { Env, StationRecord } from "./types";
import { Database } from "./db";

function parseSemVer(v: string): [number, number, number] {
  const clean = v.replace(/^v/, "").trim();
  const [base] = clean.split("-");
  const parts = base.split(".").map(p => parseInt(p, 10) || 0);
  return [parts[0] || 0, parts[1] || 0, parts[2] || 0];
}

function compareSemVer(v1: string, v2: string): number {
  const [maj1, min1, pat1] = parseSemVer(v1);
  const [maj2, min2, pat2] = parseSemVer(v2);
  if (maj1 !== maj2) return maj1 - maj2;
  if (min1 !== min2) return min1 - min2;
  if (pat1 !== pat2) return pat1 - pat2;
  // Final release has higher precedence than pre-release/canary with identical major.minor.patch
  const isPre1 = v1.includes("-");
  const isPre2 = v2.includes("-");
  if (isPre1 && !isPre2) return -1;
  if (!isPre1 && isPre2) return 1;
  return 0;
}

export async function handleOta(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);

  // 1. Firmware Binary Download Endpoint: /api/ota/download/:filename
  if (url.pathname.startsWith("/api/ota/download/")) {
    const filename = decodeURIComponent(url.pathname.substring("/api/ota/download/".length));
    return await handleFirmwareDownload(filename, request, env);
  }

  // 2. OTA Check Endpoint: GET /api/ota?station_id=...&version=...&board=...
  if (request.method !== "GET") {
    return new Response(JSON.stringify({ error: "Method Not Allowed" }), {
      status: 405,
      headers: { "Content-Type": "application/json" }
    });
  }

  const stationId = url.searchParams.get("station_id")?.trim();
  const currentVersion = url.searchParams.get("version")?.trim() || "0.0.0";
  const board = url.searchParams.get("board")?.trim() || "esp32dev";
  const clientCanary = url.searchParams.get("canary") === "1" || url.searchParams.get("track") === "canary";

  if (!stationId) {
    return new Response(JSON.stringify({ error: "Missing required query parameter: station_id" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const db = new Database(env);
  const station = (await env.DB
    .prepare("SELECT station_id, firmware_version, target_version, ota_url, is_canary FROM stations WHERE station_id = ? LIMIT 1")
    .bind(stationId)
    .first()) as (StationRecord & { is_canary?: number; ota_url?: string }) | null;

  // The database is the primary source of truth for canary entitlement
  const isCanary = station ? (station.is_canary === 1) : clientCanary;

  // Check if station has an explicit target_version assigned (e.g. for Canary testing)
  let targetVersion = station?.target_version?.trim();
  let track = isCanary ? "canary" : "stable";
  let allowDowngrade = false;

  if (targetVersion) {
    allowDowngrade = true;
    if (targetVersion.includes("canary") || targetVersion.includes("beta")) {
      track = "canary";
    }
  } else if (isCanary) {
    // Canary station:
    // If CANARY_VERSION is configured and newer, offer it; otherwise follow stable fleet release
    if (env.CANARY_VERSION && env.CANARY_VERSION.trim()) {
      targetVersion = env.CANARY_VERSION.trim();
      track = "canary";
    } else {
      targetVersion = env.STABLE_VERSION || "1.1.8";
      track = "stable";
    }
  } else {
    // Non-canary station: always follow stable production release
    targetVersion = env.STABLE_VERSION || "1.1.8";
    track = "stable";
  }

  // Version check:
  if (!allowDowngrade && compareSemVer(targetVersion, currentVersion) <= 0) {
    return new Response(
      JSON.stringify({
        update_available: false,
        station_id: stationId,
        current_version: currentVersion,
        target_version: targetVersion,
        track,
        is_canary: isCanary,
        message: "Firmware is already up to date"
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  }

  if (currentVersion === targetVersion) {
    return new Response(
      JSON.stringify({
        update_available: false,
        station_id: stationId,
        current_version: currentVersion,
        target_version: targetVersion,
        track,
        is_canary: isCanary,
        message: "Firmware is already up to date"
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  }

  // Check if already running the target version (for explicit station target_version)
  if (currentVersion === targetVersion) {
    return new Response(
      JSON.stringify({
        update_available: false,
        version: currentVersion,
        target_version: targetVersion,
        track,
        message: `Station is up-to-date running ${currentVersion}`
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  }

  // Determine binary download URL:
  // Primary: Cloudflare R2 bucket proxy via Worker
  // Fallback: GitHub Releases direct link or Worker proxy
  const isC3 = board.toLowerCase().includes("c3");
  const filename = isC3 ? `firmware-esp32c3-v${targetVersion}.bin` : `firmware-esp32-v${targetVersion}.bin`;
  let downloadUrl = station?.ota_url?.trim();

  if (!downloadUrl) {
    // Worker download route that serves from R2 or proxies from GitHub Releases
    downloadUrl = `${url.origin}/api/ota/download/${filename}`;
  }

  return new Response(
    JSON.stringify({
      update_available: true,
      station_id: stationId,
      current_version: currentVersion,
      target_version: targetVersion,
      track,
      is_canary: isCanary,
      board: board,
      allow_downgrade: allowDowngrade,
      url: downloadUrl
    }),
    {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "no-cache, no-store, must-revalidate"
      }
    }
  );
}

// Handles binary delivery with Cloudflare R2 as primary and GitHub Releases / Pages as backup
export async function handleFirmwareDownload(filename: string, request: Request, env: Env): Promise<Response> {
  const stableTag = `v${env.STABLE_VERSION || "1.1.8"}`;

  // 1. Try Cloudflare R2 Storage (Zero egress fees, high speed)
  if (env.OTA_BUCKET) {
    try {
      const r2Object = await env.OTA_BUCKET.get(filename);
      if (r2Object) {
        console.log(`[OTA] Serving ${filename} directly from Cloudflare R2 bucket`);
        const headers = new Headers();
        r2Object.writeHttpMetadata(headers);
        headers.set("Content-Type", "application/octet-stream");
        headers.set("ETag", r2Object.httpEtag);
        headers.set("Content-Disposition", `attachment; filename="${filename}"`);
        headers.set("Access-Control-Allow-Origin", "*");
        headers.set("Cache-Control", "public, max-age=86400");

        return new Response(r2Object.body, {
          status: 200,
          headers
        });
      }
    } catch (err) {
      console.warn("[OTA] R2 bucket lookup failed, falling back to GitHub Releases:", err);
    }
  }

  // 2. Resolve target tag and canonical GitHub release asset name (supports canary suffixes e.g. v1.1.8-canary.1)
  const match = filename.match(/v(\d+\.\d+\.\d+(?:-[a-zA-Z0-9\.]+)?)/);
  const tag = match ? `v${match[1]}` : stableTag;

  let releaseFilename = filename;
  if (filename === "firmware.bin") {
    releaseFilename = `firmware-esp32-${tag}.bin`;
  } else if (filename === "firmware-c3.bin" || filename === "firmware-esp32c3.bin") {
    releaseFilename = `firmware-esp32c3-${tag}.bin`;
  } else if (filename === "partitions.bin") {
    releaseFilename = `partitions-esp32-${tag}.bin`;
  } else if (filename === "partitions-c3.bin" || filename === "partitions-esp32c3.bin") {
    releaseFilename = `partitions-esp32c3-${tag}.bin`;
  } else if (filename === "bootloader.bin") {
    releaseFilename = `bootloader-esp32-${tag}.bin`;
  } else if (filename === "bootloader-c3.bin" || filename === "bootloader-esp32c3.bin") {
    releaseFilename = `bootloader-esp32c3-${tag}.bin`;
  } else if (filename === "firmware-esp32-merged.bin") {
    releaseFilename = `firmware-esp32-merged-${tag}.bin`;
  } else if (filename === "firmware-esp32c3-merged.bin") {
    releaseFilename = `firmware-esp32c3-merged-${tag}.bin`;
  }

  const githubUrl = `https://github.com/kreier/wifi/releases/download/${tag}/${releaseFilename}`;
  console.log(`[OTA] Fetching firmware binary for ${filename} (${releaseFilename}) from GitHub Releases: ${githubUrl}`);

  try {
    const ghResponse = await fetch(githubUrl, {
      redirect: "follow",
      headers: { "User-Agent": "kreier-wifi-worker" }
    });

    if (ghResponse.ok && ghResponse.body) {
      const headers = new Headers();
      headers.set("Content-Type", "application/octet-stream");
      const len = ghResponse.headers.get("Content-Length");
      if (len) headers.set("Content-Length", len);
      headers.set("Content-Disposition", `attachment; filename="${filename}"`);
      headers.set("Access-Control-Allow-Origin", "*");
      headers.set("Cache-Control", "public, max-age=86400");

      return new Response(ghResponse.body, {
        status: 200,
        headers
      });
    }

    console.warn(`[OTA] GitHub Releases returned status ${ghResponse.status} for ${githubUrl}, trying GitHub Pages static fallback...`);
  } catch (fetchErr) {
    console.warn("[OTA] Error fetching from GitHub Releases, trying GitHub Pages static fallback:", fetchErr);
  }

  // 3. GitHub Pages static fallback (e.g. for boot_app0.bin, partitions.bin, etc.)
  const pagesUrl = `https://kreier.github.io/wifi/firmware/${filename}`;
  try {
    const pagesResponse = await fetch(pagesUrl, {
      redirect: "follow",
      headers: { "User-Agent": "kreier-wifi-worker" }
    });

    if (pagesResponse.ok && pagesResponse.body) {
      const headers = new Headers();
      headers.set("Content-Type", "application/octet-stream");
      const len = pagesResponse.headers.get("Content-Length");
      if (len) headers.set("Content-Length", len);
      headers.set("Content-Disposition", `attachment; filename="${filename}"`);
      headers.set("Access-Control-Allow-Origin", "*");
      headers.set("Cache-Control", "public, max-age=86400");

      return new Response(pagesResponse.body, {
        status: 200,
        headers
      });
    }
  } catch (pagesErr) {
    console.error("[OTA] GitHub Pages fallback also failed:", pagesErr);
  }

  // Final fallback: redirect to GitHub release asset
  return Response.redirect(githubUrl, 302);
}

// Admin / Canary API: Set or clear canary target version for a station
export async function handleSetStationTargetVersion(stationId: string, request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response(JSON.stringify({ error: "Method Not Allowed" }), { status: 405 });
  }

  try {
    const body = (await request.json()) as {
      target_version?: string | null;
      is_canary?: boolean | number;
      ota_url?: string | null;
    };

    const targetVersion = body.target_version?.trim() || null;
    const isCanary = body.is_canary ? 1 : 0;
    const otaUrl = body.ota_url?.trim() || null;

    await env.DB
      .prepare(`
        UPDATE stations SET
          target_version = ?,
          is_canary = ?,
          ota_url = ?
        WHERE station_id = ?
      `)
      .bind(targetVersion, isCanary, otaUrl, stationId)
      .run();

    return new Response(
      JSON.stringify({
        success: true,
        station_id: stationId,
        target_version: targetVersion,
        is_canary: isCanary,
        ota_url: otaUrl
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  } catch (err: unknown) {
    const errMsg = err instanceof Error ? err.message : String(err);
    return new Response(JSON.stringify({ error: "Failed to update target version", message: errMsg }), {
      status: 500,
      headers: { "Content-Type": "application/json" }
    });
  }
}
