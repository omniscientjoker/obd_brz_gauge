#!/usr/bin/env bash
# Flash a package created by build_firmware_package.sh onto an ESP32-S3.
set -euo pipefail

PACKAGE_DIR="$(cd "$(dirname "$0")" && pwd)"
ERASE=false
FACTORY=false
BAUD=460800

usage() {
  cat <<'EOF'
用法：./flash_firmware.sh [--erase] [--factory] [PORT] [BAUD]

  --erase    先擦除整个 Flash，建议首次安装时使用。
  --factory  使用合并的单文件工厂固件，从地址 0x0 烧录。
  PORT       可选。省略时自动扫描可烧录串口，并通过编号选择。
             例如 /dev/cu.usbmodem1101、/dev/ttyUSB0 或 COM3。
  BAUD       可选波特率，默认 460800。
EOF
}

SERIAL_PORTS=()

add_serial_port() {
  local candidate="$1"
  local existing

  [[ -e "$candidate" ]] || return
  for existing in "${SERIAL_PORTS[@]}"; do
    [[ "$existing" == "$candidate" ]] && return
  done
  SERIAL_PORTS+=("$candidate")
}

detect_serial_ports() {
  local candidate

  case "$(uname -s)" in
    Darwin)
      shopt -s nullglob
      for candidate in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
        add_serial_port "$candidate"
      done
      ;;
    Linux)
      shopt -s nullglob
      for candidate in /dev/ttyUSB* /dev/ttyACM*; do
        add_serial_port "$candidate"
      done
      ;;
  esac
}

choose_serial_port() {
  local selection index

  detect_serial_ports
  if [[ ${#SERIAL_PORTS[@]} -eq 0 ]]; then
    echo "未检测到可烧录的 USB 串口。请连接 ESP32-S3、确认 Type-C 线支持数据传输，然后重新运行脚本。" >&2
    exit 1
  fi

  echo "检测到以下可烧录设备："
  for index in "${!SERIAL_PORTS[@]}"; do
    printf '  [%d] %s\n' "$((index + 1))" "${SERIAL_PORTS[$index]}"
  done

  read -r -p "请选择设备编号（输入 0 取消）：" selection < /dev/tty || {
    echo "未能读取设备选择，已取消烧录。" >&2
    exit 1
  }
  if [[ "$selection" == "0" ]]; then
    echo "已取消烧录。"
    exit 0
  fi
  if [[ ! "$selection" =~ ^[1-9][0-9]*$ ]] || (( selection > ${#SERIAL_PORTS[@]} )); then
    echo "设备编号无效，已取消烧录。" >&2
    exit 2
  fi

  PORT="${SERIAL_PORTS[$((selection - 1))]}"
  echo "已选择：$PORT"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --erase) ERASE=true ;;
    --factory) FACTORY=true ;;
    --help|-h) usage; exit 0 ;;
    -*) echo "未知选项：$1" >&2; usage >&2; exit 2 ;;
    *) break ;;
  esac
  shift
done

if [[ $# -gt 2 ]]; then
  usage >&2
  exit 2
fi

PORT="${1:-}"
if [[ $# -eq 2 ]]; then
  BAUD="$2"
fi

if [[ -z "$PORT" ]]; then
  choose_serial_port
fi

for required_file in flasher_args.json bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin obd_brz_gauge.bin bootmedia.bin; do
  if [[ ! -f "$PACKAGE_DIR/$required_file" ]]; then
    echo "缺少安装包文件：$PACKAGE_DIR/$required_file" >&2
    exit 1
  fi
done

if command -v esptool >/dev/null 2>&1; then
  run_esptool() { esptool "$@"; }
elif command -v esptool.py >/dev/null 2>&1; then
  run_esptool() { esptool.py "$@"; }
elif python3 -m esptool version >/dev/null 2>&1; then
  run_esptool() { python3 -m esptool "$@"; }
else
  echo "需要 esptool。请执行：python3 -m pip install esptool" >&2
  exit 1
fi

read -r FLASH_MODE FLASH_FREQ FLASH_SIZE < <(
  python3 - "$PACKAGE_DIR/flasher_args.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    settings = json.load(source)["flash_settings"]
print(settings["flash_mode"], settings["flash_freq"], settings["flash_size"])
PY
)

FLASH_ARGS=()
while IFS=$'\t' read -r offset filename; do
  [[ -n "$offset" ]] || continue
  FLASH_ARGS+=("$offset" "$PACKAGE_DIR/$filename")
done < <(
  python3 - "$PACKAGE_DIR/flasher_args.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    files = json.load(source)["flash_files"]
for offset, filename in sorted(files.items(), key=lambda item: int(item[0], 0)):
    print(f"{offset}\t{filename}")
PY
)

if [[ "$ERASE" == true ]]; then
  echo "正在擦除 $PORT 上的 Flash ..."
  run_esptool --chip esp32s3 --port "$PORT" --baud "$BAUD" erase_flash
fi

if [[ "$FACTORY" == true ]]; then
  if [[ ! -f "$PACKAGE_DIR/obd_brz_gauge_factory.bin" ]]; then
    echo "缺少工厂固件：$PACKAGE_DIR/obd_brz_gauge_factory.bin" >&2
    exit 1
  fi
  run_esptool --chip esp32s3 --port "$PORT" --baud "$BAUD" \
    --before default_reset --after hard_reset write_flash \
    --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
    0x0 "$PACKAGE_DIR/obd_brz_gauge_factory.bin"
else
  run_esptool --chip esp32s3 --port "$PORT" --baud "$BAUD" \
    --before default_reset --after hard_reset write_flash \
    --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
    "${FLASH_ARGS[@]}"
fi

echo "烧录完成，设备已自动复位。"
