#!/usr/bin/env python3
"""Compile the firmware's alert gate and threshold expressions with a fake queue."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/app_media/media_alert.c").read_text()
HEADER = (ROOT / "main/app_media/media_alert.h").read_text()


def extract_function(name):
    start = SOURCE.index(f"static void {name}(")
    opening = SOURCE.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


def threshold(alert):
    # Keep the production comparison, while omitting the cache-freshness call.
    match = re.search(
        rf"update_edge\({alert},\s*channel_fresh\([^;]+?\),\s*(.*?)\);",
        SOURCE, re.S,
    )
    assert match, alert
    return match[1]


enum = re.search(r"typedef enum \{.*?\} media_alert_type_t;", HEADER, re.S)[0]
constants = "\n".join(re.findall(r"^#define MEDIA_ALERT_(?:FUEL_LOW_PCT|VOLTAGE_LOW_MV)\s+.*$", SOURCE, re.M))
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
ENUM
CONSTANTS
static bool s_alert_latched[MEDIA_ALERT_COUNT];
static unsigned queued[MEDIA_ALERT_COUNT];
static bool queue_full;
static esp_err_t media_alert_notify(media_alert_type_t type) {
    if (queue_full) return -1;
    ++queued[type];
    return ESP_OK;
}
FUNCTION
static void boot(void) {
    memset(s_alert_latched, 0, sizeof(s_alert_latched));
    memset(queued, 0, sizeof(queued));
    queue_full = false;
}
int main(void) {
    for (int type = 0; type < MEDIA_ALERT_COUNT; ++type) {
        boot();
        update_edge(type, false, true);
        assert(queued[type] == 0);
        queue_full = true;
        update_edge(type, true, true);
        assert(queued[type] == 0);
        queue_full = false;
        update_edge(type, true, true);
        update_edge(type, true, true);
        assert(queued[type] == 1);
        // Unknown/stale samples cannot rearm any alert.
        update_edge(type, false, false);
        update_edge(type, true, true);
        assert(queued[type] == 1);
        update_edge(type, true, false);
        update_edge(type, true, true);
        bool once_per_boot = type == MEDIA_ALERT_FUEL_LOW || type == MEDIA_ALERT_VOLTAGE;
        assert(queued[type] == (once_per_boot ? 1u : 2u));
        boot();
        update_edge(type, true, true);
        assert(queued[type] == 1);
    }
    struct { int bat_mv; int fuel_pct; } obd = {0};
    struct { int tpms_voltage_min_mv; } cfg = {12000};
    (void)cfg;
    obd.bat_mv = 12999;
    assert(VOLTAGE);
    obd.bat_mv = 13000;
    assert(!(VOLTAGE));
    obd.bat_mv = 0;
    assert(!(VOLTAGE));
    obd.bat_mv = -1;
    assert(!(VOLTAGE));
    obd.fuel_pct = 9;
    assert(FUEL);
    obd.fuel_pct = 10;
    assert(!(FUEL));
    obd.fuel_pct = 0;
    assert(FUEL);
    obd.fuel_pct = -1;
    assert(!(FUEL));
    puts("Media alert policy: PASS");
}
'''
for key, value in (
    ("ENUM", enum), ("CONSTANTS", constants),
    ("FUNCTION", extract_function("update_edge")),
    ("VOLTAGE", threshold("MEDIA_ALERT_VOLTAGE")),
    ("FUEL", threshold("MEDIA_ALERT_FUEL_LOW")),
):
    harness = re.sub(rf"\b{key}\b", lambda _: value, harness)

with tempfile.TemporaryDirectory(prefix="media-alert-test-") as tmp:
    source = pathlib.Path(tmp) / "test.c"
    binary = pathlib.Path(tmp) / "test"
    source.write_text(harness)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
