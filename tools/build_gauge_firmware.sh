#!/usr/bin/env bash
set -euo pipefail

target=${1:-master}
case "$target" in
  master) build_dir=build-master; config="$PWD/sdkconfig.master"; defaults="sdkconfig.defaults;sdkconfig.defaults.master"; image=obd_brz_gauge_master.bin ;;
  slave)  build_dir=build-slave;  config="$PWD/sdkconfig.slave";  defaults="sdkconfig.defaults;sdkconfig.defaults.slave";  image=obd_brz_gauge_slave.bin  ;;
  *) echo "usage: $0 master|slave" >&2; exit 2 ;;
esac

command -v idf.py >/dev/null || { echo "idf.py is required (source ESP-IDF first)" >&2; exit 1; }

idf.py -D "SDKCONFIG=$config" -D "SDKCONFIG_DEFAULTS=$defaults" -B "$build_dir" build
cp "$build_dir/obd_brz_gauge.bin" "$build_dir/$image"
echo "$build_dir/$image"
