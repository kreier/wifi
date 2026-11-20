#!/usr/bin/env python3
import os
import re
import sys

def parse_utilization(log_file, fw_path, partition_size=1966080):
    fw_size = os.path.getsize(fw_path) if os.path.exists(fw_path) else 0
    flash_pct = (fw_size / partition_size) * 100 if partition_size else 0
    free_flash = partition_size - fw_size
    free_flash_pct = (free_flash / partition_size) * 100 if partition_size else 0

    ram_str = "Dynamic Heap"
    flash_str = f"{fw_size:,} bytes ({flash_pct:.1f}% of {partition_size:,} bytes)"

    if os.path.exists(log_file):
        log = open(log_file).read()
        ram_m = re.search(r"RAM:\s+\[[^\]]+\]\s+([\d\.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)", log)
        if ram_m:
            ram_str = f"{int(ram_m.group(2)):,} bytes ({ram_m.group(1)} of {int(ram_m.group(3)):,} bytes)"

        flash_m = re.search(r"Flash:\s+\[[^\]]+\]\s+([\d\.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)", log)
        if flash_m:
            flash_str = f"{int(flash_m.group(2)):,} bytes ({flash_m.group(1)} of {int(flash_m.group(3)):,} bytes)"

    return flash_str, ram_str, free_flash, free_flash_pct

def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else os.getenv("TAG", "v1.1.8")
    
    # ESP32 Standard
    esp32_fw = f"release_assets/firmware-esp32-{tag}.bin"
    if not os.path.exists(esp32_fw):
        if os.path.exists("firmware/.pio/build/esp32dev/firmware.bin"):
            esp32_fw = "firmware/.pio/build/esp32dev/firmware.bin"
        elif os.path.exists("docs/firmware/firmware.bin"):
            esp32_fw = "docs/firmware/firmware.bin"
            
    esp32_log = "pio_build_esp32.log" if os.path.exists("pio_build_esp32.log") else "pio_build.log"
    e32_flash, e32_ram, e32_free_flash, e32_free_pct = parse_utilization(esp32_log, esp32_fw)

    # ESP32-C3
    c3_fw = f"release_assets/firmware-esp32c3-{tag}.bin"
    if not os.path.exists(c3_fw):
        if os.path.exists("firmware/.pio/build/esp32c3/firmware.bin"):
            c3_fw = "firmware/.pio/build/esp32c3/firmware.bin"
        elif os.path.exists("docs/firmware/firmware-c3.bin"):
            c3_fw = "docs/firmware/firmware-c3.bin"

    c3_log = "pio_build_esp32c3.log"
    c3_flash, c3_ram, c3_free_flash, c3_free_pct = parse_utilization(c3_log, c3_fw)

    changelog_section = ""
    if os.path.exists("CHANGELOG.md"):
        clean_ver = tag.lstrip("v")
        with open("CHANGELOG.md", "r") as f:
            content = f.read()
            m = re.search(r"(## \[" + re.escape(clean_ver) + r"\].*?)(?=\n## \[|\Z)", content, re.DOTALL)
            if m:
                changelog_section = m.group(1).strip()

    body = f"""## Release {tag}

### ⚡ Firmware Resource Utilization

#### Standard ESP32 (Xtensa Dual-Core: LILYGO T18, T8, DevKit V1)
| Metric | Utilization | Total Allocated | Free Space |
| :--- | :--- | :--- | :--- |
| **Flash (App Slot)** | **{e32_flash}** | 1,966,080 bytes (1.9 MB OTA) | {e32_free_flash:,} bytes ({e32_free_pct:.1f}%) |
| **DRAM (RAM)** | **{e32_ram}** | 327,680 bytes (320 KB) | Dynamic Heap |

#### ESP32-C3 Super Mini (RISC-V Single-Core, Classic BT Omitted)
| Metric | Utilization | Total Allocated | Free Space |
| :--- | :--- | :--- | :--- |
| **Flash (App Slot)** | **{c3_flash}** | 1,966,080 bytes (1.9 MB OTA) | {c3_free_flash:,} bytes ({c3_free_pct:.1f}%) |
| **DRAM (RAM)** | **{c3_ram}** | 327,680 bytes (320 KB) | Dynamic Heap |

---

### 📖 Release Documentation & Highlights
{changelog_section if changelog_section else "See [CHANGELOG.md](https://github.com/kreier/wifi/blob/main/CHANGELOG.md) for full release notes."}

---

### 🚀 Installation & Flashing
- **In-Browser Web Flasher (Zero-install)**: [https://kreier.github.io/wifi](https://kreier.github.io/wifi)
- **Manual Flashing (esptool.py)**:
  - **Standard ESP32**:
    ```bash
    esptool.py --chip esp32 --baud 921600 write_flash 0x0 firmware-esp32-merged-{tag}.bin
    ```
  - **ESP32-C3 Super Mini**:
    ```bash
    esptool.py --chip esp32c3 --baud 921600 write_flash 0x0 firmware-esp32c3-merged-{tag}.bin
    ```
- **Firmware Binaries**:
  - Standard ESP32 App (OTA): `firmware-esp32-{tag}.bin` | Merged Image: `firmware-esp32-merged-{tag}.bin`
  - ESP32-C3 App (OTA): `firmware-esp32c3-{tag}.bin` | Merged Image: `firmware-esp32c3-merged-{tag}.bin`
"""
    with open("release_body.md", "w") as f:
        f.write(body)
    print(f"Generated release_body.md for {tag}")

if __name__ == "__main__":
    main()
