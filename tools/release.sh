#!/usr/bin/env bash
# Build both firmware targets and create the local release package.
# Activate ESP-IDF before running. This script does not commit or push.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

if [ "$#" -ne 0 ]; then
    echo "usage: $0 (activate ESP-IDF first)" >&2
    exit 2
fi

./tools/build_gauge_firmware.sh master
./tools/build_gauge_firmware.sh slave
python3 tools/gen_release.py
