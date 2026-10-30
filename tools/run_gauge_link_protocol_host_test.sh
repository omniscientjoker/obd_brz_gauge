#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/gauge-link-test.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/main" \
  "$repo_dir/tools/gauge_link_protocol_host_test.c" \
  "$repo_dir/main/common/gauge_link_protocol.c" \
  -o "$tmp_dir/gauge_link_protocol_host_test"
"$tmp_dir/gauge_link_protocol_host_test"
