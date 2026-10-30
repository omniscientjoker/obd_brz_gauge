#!/usr/bin/env python3
"""Exercise the real transfer-mode gates and cleanup with fake USB/SD drivers."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/app_media/usb_sd_storage.c").read_text()


def function(name):
    match = re.search(rf"^(?:static )?[\w ]+\b{name}\([^;]*?\)\s*\{{", SOURCE, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[match.start():end]


harness = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "app_media/usb_sd_storage.h"
#define ESP_RST_SW 1
#define USB_SD_BOOT_MAGIC 0x53445553u
#define ESP_LOGW(...) ((void)0)
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef void *TaskHandle_t;
static uint32_t s_boot_request;
static TaskHandle_t s_task;
static atomic_int s_state = USB_SD_STARTING, s_error;
static atomic_bool s_attached, s_usb_owns_card, s_ready;
static void *s_storage, *s_card;
static bool s_host_initialized, s_msc_initialized, s_usb_initialized;
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED };
enum { TINYUSB_MSC_EVENT_MOUNT_COMPLETE, TINYUSB_MSC_EVENT_MOUNT_FAILED,
       TINYUSB_MSC_EVENT_FORMAT_REQUIRED, TINYUSB_MSC_EVENT_MOUNT_START };
enum { TINYUSB_MSC_STORAGE_MOUNT_USB, TINYUSB_MSC_STORAGE_MOUNT_APP };
typedef struct { int id; } tinyusb_event_t;
typedef struct { int id, mount_point; } tinyusb_msc_event_t;
typedef void *tinyusb_msc_storage_handle_t;
static int reset_reason, phy_restored, notifications, pending_writes, deletes, delays;
static int cleanup_order, disconnected, create_ok = 1;
static jmp_buf restarted;
static int esp_reset_reason(void) { return reset_reason; }
static esp_err_t restore_console_phy(void) { ++phy_restored; return ESP_OK; }
static _Noreturn void esp_restart(void) { longjmp(restarted, 1); }
static void xTaskNotifyGive(TaskHandle_t task) { assert(task); ++notifications; }
static void transfer_task(void *arg) { (void)arg; }
static int xTaskCreate(void (*fn)(void *), const char *name, int size, void *arg,
                       int priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)size; (void)arg; (void)priority;
    *task = create_ok ? (void *)1 : NULL;
    return create_ok;
}
static void tud_disconnect(void) { assert(cleanup_order == 0); ++disconnected; }
static void vTaskDelay(int ticks) { assert(ticks == 20); ++delays; }
static esp_err_t tinyusb_msc_delete_storage(void *storage) {
    assert(storage && s_usb_initialized && disconnected);
    ++deletes;
    if (pending_writes-- > 0) return ESP_ERR_INVALID_STATE;
    assert(cleanup_order++ == 0);
    return ESP_OK;
}
static esp_err_t tinyusb_driver_uninstall(void) {
    assert(cleanup_order++ == 1); return ESP_OK;
}
static esp_err_t tinyusb_msc_uninstall_driver(void) {
    assert(cleanup_order++ == 2); return ESP_OK;
}
static esp_err_t sdmmc_host_deinit(void) {
    assert(cleanup_order++ == 3); return ESP_OK;
}
FUNCTIONS
int main(void) {
    // Transfer requests only survive an intentional software restart, once.
    reset_reason = ESP_RST_SW;
    s_boot_request = USB_SD_BOOT_MAGIC;
    assert(usb_sd_take_boot_request());
    assert(!s_boot_request && !phy_restored);
    assert(!usb_sd_take_boot_request() && phy_restored == 1);
    s_boot_request = USB_SD_BOOT_MAGIC;
    reset_reason = 2;
    assert(!usb_sd_take_boot_request() && !s_boot_request);

    // Startup failure must leave an exit path rather than a disabled button.
    create_ok = 0;
    assert(usb_sd_start() == ESP_ERR_NO_MEM);
    assert(usb_sd_get_snapshot().state == USB_SD_ERROR);
    assert(usb_sd_get_snapshot().error == ESP_ERR_NO_MEM);
    if (!setjmp(restarted)) { usb_sd_request_exit(); assert(false); }
    create_ok = 1;
    s_state = USB_SD_STARTING;
    assert(usb_sd_start() == ESP_OK);
    assert(usb_sd_request_exit() == ESP_ERR_INVALID_STATE);
    assert(usb_sd_start() == ESP_ERR_INVALID_STATE);

    s_state = USB_SD_WAITING_HOST;
    s_ready = true;
    assert(usb_sd_get_snapshot().state == USB_SD_WAITING_HOST);
    tinyusb_event_t usb = {.id = TINYUSB_EVENT_ATTACHED};
    usb_event(&usb, NULL);
    tinyusb_msc_event_t storage = {
        .id = TINYUSB_MSC_EVENT_MOUNT_COMPLETE,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    storage_event(NULL, &storage, NULL);
    assert(usb_sd_get_snapshot().state == USB_SD_CONNECTED);
    assert(usb_sd_request_exit() == ESP_ERR_INVALID_STATE && !notifications);
    storage.id = TINYUSB_MSC_EVENT_FORMAT_REQUIRED;
    storage_event(NULL, &storage, NULL);
    assert(usb_sd_request_exit() == ESP_ERR_INVALID_STATE);
    storage.id = TINYUSB_MSC_EVENT_MOUNT_COMPLETE;
    storage.mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP;
    storage_event(NULL, &storage, NULL);
    assert(usb_sd_get_snapshot().state == USB_SD_EJECTED);
    assert(usb_sd_request_exit() == ESP_OK && notifications == 1);
    usb.id = TINYUSB_EVENT_DETACHED;
    usb_event(&usb, NULL);
    assert(usb_sd_get_snapshot().state == USB_SD_WAITING_HOST);

    // USB stays alive until deferred writes finish; only then free the card.
    s_storage = (void *)1;
    s_card = malloc(1);
    s_host_initialized = s_msc_initialized = s_usb_initialized = true;
    pending_writes = 2;
    assert(stop_transfer() == ESP_OK);
    assert(deletes == 3 && delays == 2 && cleanup_order == 4);
    assert(!s_storage && !s_card && !s_host_initialized && !s_usb_initialized);
    s_storage = (void *)1;
    s_card = malloc(1);
    s_host_initialized = s_msc_initialized = s_usb_initialized = true;
    pending_writes = 100;
    cleanup_order = disconnected = deletes = delays = 0;
    assert(stop_transfer() == ESP_ERR_INVALID_STATE);
    assert(deletes == 50 && !cleanup_order && s_storage && s_card);
    assert(s_usb_initialized && s_host_initialized);
    pending_writes = 0;
    assert(stop_transfer() == ESP_OK && cleanup_order == 4);
    puts("USB SD lifecycle: PASS");
}
'''
names = ["usb_sd_take_boot_request", "usb_event", "storage_event", "stop_transfer",
         "usb_sd_start", "usb_sd_request_exit", "usb_sd_get_snapshot"]
harness = harness.replace("FUNCTIONS", "\n".join(function(name) for name in names))

# SD transfer must never enable auto-format or start the normal media mount.
assert ".do_not_format = true" in function("start_transfer")
app = (ROOT / "main/app_main.c").read_text()
branch = app[app.index("if (usb_transfer_boot) {"):app.index("/* 6.5 Initialize Bluetooth")]
assert "return;" in branch and "start_media_services()" not in branch
assert app.index("if (usb_transfer_boot)") < app.index("elm327_ble_ensure_stack_init();")

with tempfile.TemporaryDirectory(prefix="usb-sd-test-") as tmp:
    directory = Path(tmp)
    (directory / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_SUPPORTED 3
""")
    test = directory / "test.c"
    binary = directory / "test"
    test.write_text(harness)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{directory}",
                    f"-I{ROOT / 'main'}", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
