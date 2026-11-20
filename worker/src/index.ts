import { Env } from "./types";
import { handleRegister } from "./register";
import { handleTelemetry } from "./telemetry";
import { handleApiStations, handleApiStationDetail, handleApiExport } from "./api";
import { handleOta, handleSetStationTargetVersion, handleFirmwareDownload } from "./ota";
import { handleScheduledMaintenance } from "./cron";
import { renderDashboardHtml } from "./frontend";
import { handleAdminApi } from "./admin";

export default {
  async fetch(request: Request, env: Env, _ctx: ExecutionContext): Promise<Response> {
    const url = new URL(request.url);
    const path = url.pathname;

    // Handle CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, {
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization",
          "Access-Control-Max-Age": "86400"
        }
      });
    }

    try {
      // 1. Zero-Trust Station Registration
      if (path === "/register") {
        return await handleRegister(request, env);
      }

      // 2. Telemetry Ingestion
      if (path === "/data") {
        return await handleTelemetry(request, env);
      }

      // 3. OTA Firmware Updates and Downloads
      if (path === "/api/ota" || path.startsWith("/api/ota/")) {
        return await handleOta(request, env);
      }

      // 4. Web Flasher Manifests & Firmware Assets
      if (path === "/manifest-c3-update.json" || path === "/firmware/manifest-c3-update.json" || path === "/manifest-esp32c3-update.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-C3 (Update)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_skip_erase: true,
          builds: [
            {
              chipFamily: "ESP32-C3",
              parts: [
                { path: "firmware/bootloader-c3.bin", offset: 0 },
                { path: "firmware/partitions-c3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c3.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-c3.json" || path === "/firmware/manifest-c3.json" || path === "/manifest-esp32c3.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-C3",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_prompt_erase: true,
          builds: [
            {
              chipFamily: "ESP32-C3",
              parts: [
                { path: "firmware/bootloader-c3.bin", offset: 0 },
                { path: "firmware/partitions-c3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c3.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-esp32-update.json" || path === "/firmware/manifest-esp32-update.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32 (Update)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_skip_erase: true,
          builds: [
            {
              chipFamily: "ESP32",
              parts: [
                { path: "firmware/bootloader.bin", offset: 4096 },
                { path: "firmware/partitions.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-esp32.json" || path === "/firmware/manifest-esp32.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_prompt_erase: true,
          builds: [
            {
              chipFamily: "ESP32",
              parts: [
                { path: "firmware/bootloader.bin", offset: 4096 },
                { path: "firmware/partitions.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-s3-update.json" || path === "/firmware/manifest-s3-update.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-S3 (Update)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_skip_erase: true,
          builds: [
            {
              chipFamily: "ESP32-S3",
              parts: [
                { path: "firmware/bootloader-s3.bin", offset: 0 },
                { path: "firmware/partitions-s3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-s3.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-s3.json" || path === "/firmware/manifest-s3.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-S3",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_prompt_erase: true,
          builds: [
            {
              chipFamily: "ESP32-S3",
              parts: [
                { path: "firmware/bootloader-s3.bin", offset: 0 },
                { path: "firmware/partitions-s3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-s3.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-c5-update.json" || path === "/firmware/manifest-c5-update.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-C5 (Update)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_skip_erase: true,
          builds: [
            {
              chipFamily: "ESP32-C5",
              parts: [
                { path: "firmware/bootloader-c5.bin", offset: 0 },
                { path: "firmware/partitions-c5.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c5.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-c5.json" || path === "/firmware/manifest-c5.json") {
        const manifest = {
          name: "Wi-Fi Monitor - ESP32-C5 (Wi-Fi 6 / 5 GHz)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_prompt_erase: true,
          builds: [
            {
              chipFamily: "ESP32-C5",
              parts: [
                { path: "firmware/bootloader-c5.bin", offset: 0 },
                { path: "firmware/partitions-c5.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c5.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest-update.json" || path === "/firmware/manifest-update.json") {
        const manifest = {
          name: "Wi-Fi Monitor (Update)",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_skip_erase: true,
          builds: [
            {
              chipFamily: "ESP32",
              parts: [
                { path: "firmware/bootloader.bin", offset: 4096 },
                { path: "firmware/partitions.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-C3",
              parts: [
                { path: "firmware/bootloader-c3.bin", offset: 0 },
                { path: "firmware/partitions-c3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c3.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-S3",
              parts: [
                { path: "firmware/bootloader-s3.bin", offset: 0 },
                { path: "firmware/partitions-s3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-s3.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-C5",
              parts: [
                { path: "firmware/bootloader-c5.bin", offset: 0 },
                { path: "firmware/partitions-c5.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c5.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path === "/manifest.json" || path === "/firmware/manifest.json") {
        const manifest = {
          name: "Wi-Fi Monitor",
          version: env.STABLE_VERSION || "1.1.8",
          funding_url: "https://github.com/kreier/wifi",
          new_install_prompt_erase: true,
          builds: [
            {
              chipFamily: "ESP32",
              parts: [
                { path: "firmware/bootloader.bin", offset: 4096 },
                { path: "firmware/partitions.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-C3",
              parts: [
                { path: "firmware/bootloader-c3.bin", offset: 0 },
                { path: "firmware/partitions-c3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c3.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-S3",
              parts: [
                { path: "firmware/bootloader-s3.bin", offset: 0 },
                { path: "firmware/partitions-s3.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-s3.bin", offset: 65536 }
              ]
            },
            {
              chipFamily: "ESP32-C5",
              parts: [
                { path: "firmware/bootloader-c5.bin", offset: 0 },
                { path: "firmware/partitions-c5.bin", offset: 32768 },
                { path: "firmware/boot_app0.bin", offset: 57344 },
                { path: "firmware/firmware-c5.bin", offset: 65536 }
              ]
            }
          ]
        };
        return new Response(JSON.stringify(manifest, null, 2), {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            "Access-Control-Allow-Origin": "*",
            "Cache-Control": "public, max-age=3600"
          }
        });
      }

      if (path.startsWith("/firmware/")) {
        const filename = decodeURIComponent(path.substring("/firmware/".length));
        return await handleFirmwareDownload(filename, request, env);
      }

      if (path.startsWith("/api/station/") && path.endsWith("/target-version")) {
        const parts = path.split("/");
        const stationId = decodeURIComponent(parts[3]);
        return await handleSetStationTargetVersion(stationId, request, env);
      }

      // 3. Public Read-only APIs
      if (path === "/api/stations" && request.method === "GET") {
        return await handleApiStations(request, env);
      }

      if (path.startsWith("/api/station/") && request.method === "GET") {
        const stationId = decodeURIComponent(path.substring("/api/station/".length));
        return await handleApiStationDetail(stationId, request, env);
      }

      if (path === "/api/export" && request.method === "GET") {
        return await handleApiExport(request, env);
      }

      // 4. Admin & Fleet Management APIs
      if (path.startsWith("/api/admin/")) {
        const subPath = path.substring("/api/admin/".length);
        return await handleAdminApi(subPath, request, env);
      }

      // 5. Web Dashboard Frontend (Root or direct station route e.g. /9E1-9)
      if ((request.method === "GET" || request.method === "HEAD") && !path.startsWith("/api/")) {
        return new Response(request.method === "HEAD" ? null : renderDashboardHtml(), {
          status: 200,
          headers: {
            "Content-Type": "text/html; charset=utf-8",
            "Cache-Control": "public, max-age=60"
          }
        });
      }

      // 404 Fallback
      return new Response(JSON.stringify({ error: "Not Found", path }), {
        status: 404,
        headers: { "Content-Type": "application/json" }
      });
    } catch (err: unknown) {
      console.error("[Worker Error]", err);
      const errMsg = err instanceof Error ? err.message : String(err);
      return new Response(JSON.stringify({ error: "Internal Server Error", message: errMsg }), {
        status: 500,
        headers: { "Content-Type": "application/json" }
      });
    }
  },

  async scheduled(event: ScheduledEvent, env: Env, _ctx: ExecutionContext): Promise<void> {
    await handleScheduledMaintenance(event, env);
  }
};
