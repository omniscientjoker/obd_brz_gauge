#include "app_media/usb_sd_storage.h"

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_system.h"

#if CONFIG_GAUGE_FIRMWARE_MASTER && CONFIG_OBD_HW_VERSION_V1_WAVESHARE && CONFIG_TINYUSB_MSC_ENABLED && CONFIG_TINYUSB_CDC_ENABLED

#include <stdatomic.h>
#include <stdlib.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"

#define USB_SD_BOOT_MAGIC 0x53445553u
static RTC_NOINIT_ATTR uint32_t s_boot_request;
static const char *TAG = "usb_sd";
static TaskHandle_t s_task;
static atomic_int s_state = USB_SD_STARTING;
static atomic_int s_error = ESP_OK;
static atomic_bool s_attached;
static atomic_bool s_usb_owns_card;
static atomic_bool s_ready;
static sdmmc_card_t *s_card;
static tinyusb_msc_storage_handle_t s_storage;
static bool s_host_initialized;
static bool s_msc_initialized;
static bool s_usb_initialized;
static usb_phy_handle_t s_console_phy;

static esp_err_t restore_console_phy(void)
{
    if (s_console_phy) return ESP_OK;
    const usb_phy_config_t config = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
    };
    /* The RTC PHY mux survives a CPU restart. Explicitly return it to USJ,
     * including a normal boot following a crash in file-transfer mode. */
    return usb_new_phy(&config, &s_console_phy);
}

bool usb_sd_supported(void)
{
    return true;
}

bool usb_sd_take_boot_request(void)
{
    bool requested = esp_reset_reason() == ESP_RST_SW && s_boot_request == USB_SD_BOOT_MAGIC;
    s_boot_request = 0;
    if (!requested) {
        esp_err_t err = restore_console_phy();
        if (err != ESP_OK) ESP_LOGW(TAG, "Console PHY restore failed: %s", esp_err_to_name(err));
    }
    return requested;
}

void usb_sd_reboot_into_transfer(void)
{
    s_boot_request = USB_SD_BOOT_MAGIC;
    esp_restart();
}

/* These callbacks only publish state. LVGL is updated by the UI timer. */
static void usb_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        atomic_store(&s_attached, true);
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        atomic_store(&s_attached, false);
    }
}

static void storage_event(tinyusb_msc_storage_handle_t handle,
                          tinyusb_msc_event_t *event, void *arg)
{
    (void)handle;
    (void)arg;
    if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
        atomic_store(&s_usb_owns_card, event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB);
    } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED ||
               event->id == TINYUSB_MSC_EVENT_FORMAT_REQUIRED) {
        /* Never format a user's card. Keep ownership conservative on failure. */
        ESP_LOGW(TAG, "SD mount event %d; card will not be formatted", event->id);
    }
}

static esp_err_t init_card(void)
{
    s_card = calloc(1, sizeof(*s_card));
    if (!s_card) return ESP_ERR_NO_MEM;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    /* Same SDMMC wiring as sd_media_manager.c; no FAT/VFS mount here. */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_15;
    slot.cmd = GPIO_NUM_14;
    slot.d0 = GPIO_NUM_16;
    slot.d1 = GPIO_NUM_17;
    slot.d2 = GPIO_NUM_12;
    slot.d3 = GPIO_NUM_13;
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags &= ~SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = host.init();
    if (err != ESP_OK) return err;
    s_host_initialized = true;
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) return err;
    return sdmmc_card_init(&host, s_card);
}

static esp_err_t start_transfer(void)
{
    esp_err_t err = init_card();
    if (err != ESP_OK) return err;

    const tinyusb_msc_driver_config_t msc_cfg = {
        .callback = storage_event,
    };
    err = tinyusb_msc_install_driver(&msc_cfg);
    if (err != ESP_OK) return err;
    s_msc_initialized = true;
    const tinyusb_msc_storage_config_t storage_cfg = {
        .medium.card = s_card,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = {
            .base_path = "/usb_sd",
            .config.max_files = 2,
            .do_not_format = true,
        },
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &s_storage);
    if (err != ESP_OK) return err;
    atomic_store(&s_usb_owns_card, true);

    /* The internal PHY is shared with USB Serial/JTAG. Only this boot mode
     * hands it to OTG; the normal gauge boot keeps the existing console. */
    tinyusb_config_t usb_cfg = TINYUSB_DEFAULT_CONFIG();
    usb_cfg.event_cb = usb_event;
    err = tinyusb_driver_install(&usb_cfg);
    if (err != ESP_OK) return err;
    s_usb_initialized = true;
    const tinyusb_config_cdcacm_t cdc_cfg = {.cdc_port = TINYUSB_CDC_ACM_0};
    err = tinyusb_cdcacm_init(&cdc_cfg);
    if (err == ESP_OK) err = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CDC console unavailable: %s; MSC remains active", esp_err_to_name(err));
    }
    return ESP_OK;
}

static esp_err_t stop_transfer(void)
{
    /* Disconnect the pull-up first. Keep the USB task alive while deferred
     * writes drain; deleting storage refuses any pending writes. */
    if (s_usb_initialized) {
        tud_disconnect();
        atomic_store(&s_attached, false);
    }
    if (s_storage) {
        esp_err_t err = ESP_ERR_INVALID_STATE;
        for (unsigned retry = 0; retry < 50; ++retry) {
            err = tinyusb_msc_delete_storage(s_storage);
            if (err != ESP_ERR_INVALID_STATE) break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (err != ESP_OK) return err;
        s_storage = NULL;
    }
    if (s_usb_initialized) {
        esp_err_t err = tinyusb_driver_uninstall();
        if (err != ESP_OK) return err;
        s_usb_initialized = false;
    }
    if (s_msc_initialized) {
        esp_err_t err = tinyusb_msc_uninstall_driver();
        if (err != ESP_OK) return err;
        s_msc_initialized = false;
    }
    if (s_host_initialized) {
        esp_err_t err = sdmmc_host_deinit();
        if (err != ESP_OK) return err;
        s_host_initialized = false;
    }
    free(s_card);
    s_card = NULL;
    return ESP_OK;
}

static void transfer_task(void *arg)
{
    (void)arg;
    esp_err_t err = start_transfer();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start SD transfer: %s", esp_err_to_name(err));
        atomic_store(&s_error, err);
        atomic_store(&s_state, USB_SD_ERROR);
    } else {
        atomic_store(&s_ready, true);
        atomic_store(&s_state, USB_SD_WAITING_HOST);
        ESP_LOGI(TAG, "USB SD transfer ready; eject SD on the computer before exiting");
    }

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* Recheck after the UI request, including a host reconnect race. */
        if (atomic_load(&s_attached) && atomic_load(&s_usb_owns_card)) continue;
        atomic_store(&s_ready, false);
        atomic_store(&s_state, USB_SD_STOPPING);
        err = stop_transfer();
        if (err == ESP_OK) err = restore_console_phy();
        if (err == ESP_OK) {
            s_boot_request = 0;
            esp_restart();
        }
        atomic_store(&s_error, err);
        atomic_store(&s_state, USB_SD_ERROR);
    }
}

esp_err_t usb_sd_start(void)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(transfer_task, "usb_sd", 6144, NULL, 5, &s_task) != pdPASS) {
        atomic_store(&s_error, ESP_ERR_NO_MEM);
        atomic_store(&s_state, USB_SD_ERROR);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t usb_sd_request_exit(void)
{
    if (atomic_load(&s_state) == USB_SD_STARTING ||
        atomic_load(&s_state) == USB_SD_STOPPING) return ESP_ERR_INVALID_STATE;
    if (!s_task) {
        /* Task allocation failed before a host could access the card. */
        s_boot_request = 0;
        esp_restart();
    }
    if (atomic_load(&s_attached) && atomic_load(&s_usb_owns_card)) return ESP_ERR_INVALID_STATE;
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

usb_sd_snapshot_t usb_sd_get_snapshot(void)
{
    usb_sd_snapshot_t result = {
        .state = atomic_load(&s_state),
        .error = atomic_load(&s_error),
    };
    if (atomic_load(&s_ready)) {
        result.state = !atomic_load(&s_attached) ? USB_SD_WAITING_HOST :
                       atomic_load(&s_usb_owns_card) ? USB_SD_CONNECTED : USB_SD_EJECTED;
    }
    return result;
}

#else
bool usb_sd_supported(void) { return false; }
bool usb_sd_take_boot_request(void) { return false; }
void usb_sd_reboot_into_transfer(void) { }
esp_err_t usb_sd_start(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t usb_sd_request_exit(void) { return ESP_ERR_NOT_SUPPORTED; }
usb_sd_snapshot_t usb_sd_get_snapshot(void)
{
    return (usb_sd_snapshot_t){.state = USB_SD_ERROR, .error = ESP_ERR_NOT_SUPPORTED};
}
#endif
