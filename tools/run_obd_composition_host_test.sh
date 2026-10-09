#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/obd-composition-test.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/main" -I"$repo_dir/main/app_obd_dsp" \
  "$repo_dir/tools/obd_composition_host_test.c" \
  "$repo_dir/main/app_obd_dsp/obd_composition.c" \
  "$repo_dir/main/app_obd_dsp/obd_vehicle_compositions.c" \
  "$repo_dir/main/app_obd_dsp/obd_response_dispatch.c" \
  "$repo_dir/main/app_obd_dsp/obd_protocol_registry.c" \
  "$repo_dir/main/app_obd_dsp/obd_request_plan.c" \
  "$repo_dir/main/app_obd_dsp/obd_channel_arbiter.c" \
  "$repo_dir/main/app_obd_dsp/ford_mondeo_tpms.c" \
  "$repo_dir/main/app_obd_dsp/obd_special_bmw.c" \
  "$repo_dir/main/app_obd_dsp/obd_special_mode21.c" \
  -o "$tmp_dir/obd_composition_host_test"
"$tmp_dir/obd_composition_host_test"
