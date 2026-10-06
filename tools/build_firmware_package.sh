#!/usr/bin/env bash
# Build the ESP32-S3 firmware and assemble a flashable package in ../dir.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build.idf-v5.5.3}"
OUTPUT_DIR="${1:-$ROOT/dir}"
PACKAGE_COMPLETE=false

report_result() {
  local status=$?

  echo
  if [[ "$status" -eq 0 && "$PACKAGE_COMPLETE" == true ]]; then
    echo "========== 打包成功 =========="
    echo "安装包目录：$OUTPUT_DIR"
    echo "工厂固件：$OUTPUT_DIR/obd_brz_gauge_factory.bin"
  else
    echo "========== 打包失败 ==========" >&2
    echo "退出状态：$status" >&2
    echo "输出目录：$OUTPUT_DIR" >&2
  fi
}

trap report_result EXIT

if [[ "$OUTPUT_DIR" == "/" || "$OUTPUT_DIR" == "$ROOT" ]]; then
  echo "Refusing to use the filesystem or project root as the package directory." >&2
  exit 2
fi

activate_idf() {
  if command -v idf.py >/dev/null 2>&1; then
    return
  fi

  local activate_script export_script
  activate_script="$(find "$HOME/.espressif/tools" -maxdepth 1 -name 'activate_idf_*.sh' -type f 2>/dev/null | head -1 || true)"
  if [[ -n "$activate_script" ]]; then
    eval "$(bash -c '. "$1" >/dev/null 2>&1; export -p' bash "$activate_script" \
      | grep -E '^declare -x (IDF_TOOLS_PATH|IDF_PATH|ESP_ROM_ELF_DIR|OPENOCD_SCRIPTS|IDF_PYTHON_ENV_PATH|ESP_IDF_VERSION|VIRTUAL_ENV|PATH)=' || true)"
    return
  fi

  export_script="$(find "$HOME/.espressif" -path '*/esp-idf/export.sh' -type f 2>/dev/null | head -1 || true)"
  if [[ -n "$export_script" ]]; then
    set +u
    # shellcheck disable=SC1090
    . "$export_script" >/dev/null
    set -u
  fi
}

activate_idf

IDF_PATH="${IDF_PATH:-$HOME/.espressif/v5.5.3/esp-idf}"
IDF_PY="${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python}"
if [[ -z "$IDF_PY" || ! -x "$IDF_PY" ]]; then
  IDF_PY="$(find "$HOME/.espressif/python_env" -path '*/bin/python' -type f 2>/dev/null | sort | tail -1 || true)"
fi

if [[ ! -f "$IDF_PATH/tools/idf.py" || ! -x "$IDF_PY" ]]; then
  echo "ESP-IDF 5.5.x was not found. Run its export.sh once, then retry." >&2
  exit 1
fi

cd "$ROOT"
"$IDF_PY" "$IDF_PATH/tools/idf.py" -B "$BUILD_DIR" build

BUILD_TARGET="$("$IDF_PY" - "$BUILD_DIR/project_description.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    print(json.load(source)["target"])
PY
)"
if [[ "$BUILD_TARGET" != "esp32s3" ]]; then
  echo "Build target is not ESP32-S3; package generation stopped." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/bootloader" "$OUTPUT_DIR/partition_table"

copy_artifact() {
  local source="$1"
  local destination="$2"
  if [[ ! -f "$source" ]]; then
    echo "Expected build artifact is missing: $source" >&2
    exit 1
  fi
  cp "$source" "$destination"
}

copy_artifact "$BUILD_DIR/bootloader/bootloader.bin" "$OUTPUT_DIR/bootloader/bootloader.bin"
copy_artifact "$BUILD_DIR/partition_table/partition-table.bin" "$OUTPUT_DIR/partition_table/partition-table.bin"
copy_artifact "$BUILD_DIR/ota_data_initial.bin" "$OUTPUT_DIR/ota_data_initial.bin"
copy_artifact "$BUILD_DIR/obd_brz_gauge.bin" "$OUTPUT_DIR/obd_brz_gauge.bin"
copy_artifact "$BUILD_DIR/bootmedia.bin" "$OUTPUT_DIR/bootmedia.bin"
copy_artifact "$BUILD_DIR/flasher_args.json" "$OUTPUT_DIR/flasher_args.json"
copy_artifact "$BUILD_DIR/flash_args" "$OUTPUT_DIR/flash_args"

ESPTOOL_PY="$IDF_PATH/components/esptool_py/esptool/esptool.py"
if [[ ! -f "$ESPTOOL_PY" ]]; then
  echo "ESP-IDF esptool.py is missing: $ESPTOOL_PY" >&2
  exit 1
fi

"$IDF_PY" "$ESPTOOL_PY" --chip esp32s3 merge_bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  --output "$OUTPUT_DIR/obd_brz_gauge_factory.bin" \
  0x0 "$OUTPUT_DIR/bootloader/bootloader.bin" \
  0x8000 "$OUTPUT_DIR/partition_table/partition-table.bin" \
  0xf000 "$OUTPUT_DIR/ota_data_initial.bin" \
  0x20000 "$OUTPUT_DIR/obd_brz_gauge.bin" \
  0xa20000 "$OUTPUT_DIR/bootmedia.bin"

cp "$ROOT/tools/flash_firmware.sh" "$OUTPUT_DIR/flash_firmware.sh"
chmod +x "$OUTPUT_DIR/flash_firmware.sh"

cat > "$OUTPUT_DIR/FLASH_INSTRUCTIONS.txt" <<'EOF'
ESP32-S3 16 MB 固件安装包

首次安装（擦除全部旧数据）：
  ./flash_firmware.sh --erase

后续更新（保留 NVS 设置）：
  ./flash_firmware.sh

使用单文件工厂固件，从地址 0x0 烧录：
  ./flash_firmware.sh --factory --erase

脚本会自动列出可烧录串口并要求选择。请使用可传输数据的 Type-C 线。
EOF

(
  cd "$OUTPUT_DIR"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 shasum -a 256 > SHA256SUMS
)

PACKAGE_COMPLETE=true
