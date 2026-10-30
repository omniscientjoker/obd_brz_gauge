#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    USB_SD_STARTING,
    USB_SD_WAITING_HOST,
    USB_SD_CONNECTED,
    USB_SD_EJECTED,
    USB_SD_STOPPING,
    USB_SD_ERROR,
} usb_sd_state_t;

typedef struct {
    usb_sd_state_t state;
    esp_err_t error;
} usb_sd_snapshot_t;

bool usb_sd_supported(void);
/* One-shot request survives esp_restart(), but not power loss or a crash. */
bool usb_sd_take_boot_request(void);
void usb_sd_reboot_into_transfer(void);
/* Call only on a transfer boot, before starting BLE, SD media or playback. */
esp_err_t usb_sd_start(void);
/* Reject exit while an attached host still owns the card. */
esp_err_t usb_sd_request_exit(void);
usb_sd_snapshot_t usb_sd_get_snapshot(void);
