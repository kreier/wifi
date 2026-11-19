#!/usr/bin/env python3
import os
import re
import sys

def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else os.getenv("TAG", "v1.1.3")
    fw_path = f"release_assets/firmware-esp32-{tag}.bin"
    if not os.path.exists(fw_path):
        if os.path.exists("docs/firmware/firmware.bin"):
            fw_path = "docs/firmware/firmware.bin"
        elif os.path.exists("firmware/.pio/build/esp32dev/firmware.bin"):
            fw_path = "firmware/.pio/build/esp32dev/firmware.bin"
    fw_size = os.path.getsize(fw_path) if os.path.exists(fw_path) else 0

    partition_size = 1966080  # 1.9 MB OTA slot in min_spiffs.csv
    flash_pct = (fw_size / partition_size) * 100 if partition_size else 0
    free_flash = partition_size - fw_size
    free_flash_pct = (free_flash / partition_size) * 100 if partition_size else 0

    ram_str = "79,332 bytes (24.2% of 327,680 bytes)"
    flash_str = f"{fw_size:,} bytes ({flash_pct:.1f}% of {partition_size:,} bytes)"

    if os.path.exists("pio_build.log"):
        log = open("pio_build.log").read()
        ram_m = re.search(r"RAM:\s+\[[^\]]+\]\s+([\d\.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)", log)
        if ram_m:
            ram_str = f"{int(ram_m.group(2)):,} bytes ({ram_m.group(1)} of {int(ram_m.group(3)):,} bytes)"
        flash_m = re.search(r"Flash:\s+\[[^\]]+\]\s+([\d\.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)", log)
        if flash_m:
            flash_str = f"{int(flash_m.group(2)):,} bytes ({flash_m.group(1)} of {int(flash_m.group(3)):,} bytes)"

    changelog_section = ""
    if os.path.exists("CHANGELOG.md"):
        clean_ver = tag.lstrip("v")
        content = open("CHANGELOG.md").read()
        m = re.search(r"## \[" + re.escape(clean_ver) + r"\][^\n]*\n(.*?)(?=\n## \[|\Z)", content, re.DOTALL)
        if m:
            changelog_section = m.group(1).strip()

    body = f"""## Release {tag}

### ⚡ ESP32 Firmware Resource Utilization
| Metric | Utilization | Total Allocated | Free Space |
| :--- | :--- | :--- | :--- |
| **Flash (App Slot)** | **{flash_str}** | 1,966,080 bytes (1.9 MB OTA) | {free_flash:,} bytes ({free_flash_pct:.1f}%) |
| **DRAM (RAM)** | **{ram_str}** | 327,680 bytes (320 KB) | Dynamic Heap |

---

### 📖 Release Documentation & Highlights
{changelog_section if changelog_section else "See [CHANGELOG.md](https://github.com/kreier/wifi/blob/main/CHANGELOG.md) for full release notes."}

---

### 🚀 Installation & Flashing
- **In-Browser Web Flasher (Zero-install)**: [https://kreier.github.io/wifi](https://kreier.github.io/wifi)
- **Manual Flashing (esptool.py)**:
  ```bash
  esptool.py --chip esp32 --baud 921600 write_flash 0x0 firmware-esp32-merged-{tag}.bin
  ```
- **Firmware Binary (App only at 0x10000 or via OTA)**: `firmware-esp32-{tag}.bin`
- **Full Factory Image (Bootloader + Partitions + App at 0x0)**: `firmware-esp32-merged-{tag}.bin`
"""
    with open("release_body.md", "w") as f:
        f.write(body)
    print(f"Generated release_body.md for {tag} successfully.")

if __name__ == "__main__":
    main()
