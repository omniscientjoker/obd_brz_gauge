#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "export_path/ui.h"
#include "app_obd_dsp/vehicle_profiles.h"
#include "espnow_link.h"   // ESPNOW_ROLE_* (device_role default / bounds)

#define TAG                   "nvs_storage"
#define NS_CFG                "cfg"
#define KEY_CFG               "settings"
#define KEY_CHART_ALARM       "chartalarm"
#define CHART_ALARM_N         DISP_ITEM_COUNT
#define CHART_ALARM_OFF       32767 // "off" sentinel for alarm thresholds (unreachable, avoids false alarms)
#define KEY_MG_EXTRA          "mgextra"   // multi-gauge boot animation settings
#define KEY_CFG_VERSION       "cfgver"    // config version (missing = v0)
#define CFG_VERSION_CURRENT   4           // current version; bump on field add/semantic change (migration in nvs_storage_init)
#define INTRO_MODE_COUNT      3

static nvs_user_cfg_t s_cfg =   {
                        .protocol = 0, // OBD protocol select: 0=auto, 1~9=fixed, default auto
                        .theme_cfg.theme = 0,// UI theme index (0=DEFAULT, registry in ui_theme.c)
                        .theme_cfg.user_theme_domiant_color = COLOR_DOMIANT_PINK,// legacy, unused (kept for struct layout)
                        .theme_cfg.user_theme_secondary_color = COLOR_SECONDARY_PINK,// legacy, unused
                        .ble_device_name = "", // empty = use default "OBDII"
                        .temp_display_map = {0, 2, 1}, // CLT, OIL, IAT (simulator order)
                        .info_display_map = {5, 6, 0, 7, 1}, // RPM, SPEED, CLT, BAT, IAT
                        .brake_temp_warn_c = 600,
                        .oil_pressure_warn_x10 = 80,
                        .device_role = ESPNOW_ROLE_STANDALONE, // new devices (no cfg in NVS) default to standalone (no WiFi/ESP-NOW); existing devices are overridden by load_blob
                        .rpm_warn_threshold = 6000,
                        .rpm_warn_anim_en = 0,
                        .rpm_warn_linked_en = 0,
                        .tpms_pressure_min_bar_x100 = 200,
                        .tpms_pressure_max_bar_x100 = 320,
                        .tpms_voltage_min_mv = 12000,
                    };
static nvs_stat_t     s_stat = {0};   // runtime-only stats, not persisted (reset every boot to save flash)
static SemaphoreHandle_t s_mux;

typedef enum {
    LOAD_BLOB_FOUND = 0,
    LOAD_BLOB_NOT_FOUND,
    LOAD_BLOB_TRUNCATED,
    LOAD_BLOB_ERROR,
} load_blob_result_t;

// Per-item alarm thresholds (raw units), index = disp_item_t: CLT,IAT,OIL,LOD,TPS,RPM,SPD,BAT,OIP,BKT,BST,AFR
// By default only oil pressure (8.0bar = x10 80) and brake temp (600°C = x10 6000) keep an alarm; the rest are off.
static int16_t s_chart_alarm[CHART_ALARM_N] = {
    CHART_ALARM_OFF, CHART_ALARM_OFF, CHART_ALARM_OFF, CHART_ALARM_OFF,
    CHART_ALARM_OFF, CHART_ALARM_OFF, CHART_ALARM_OFF, CHART_ALARM_OFF,
    80, 6000, CHART_ALARM_OFF, CHART_ALARM_OFF
};

// Multi-gauge boot animation settings (separate blob):
// intro_enable 0=OFF 1=RACE 2=VIDEO (the single boot_block flashed via the phone app; default)
static struct __attribute__((packed)) {
    uint8_t intro_enable;   // 0=OFF 1=RACE 2=VIDEO
    uint8_t device_position; // 1/2/3
    uint8_t boot_mode;      // 0=default animation, 1=custom image, 2=video
} s_mg = { 2, 1, 0 };

/* Forward declarations */
static load_blob_result_t load_blob(const char *ns,const char *key,void *out,size_t len);
static esp_err_t save_blob(const char *ns,const char *key,const void *data,size_t len);
static void cfg_normalize(nvs_user_cfg_t *cfg);
static bool cfg_validate(const nvs_user_cfg_t *cfg);

esp_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Create the lock before any accessor can be reached by another task. */
    if (!s_mux) {
        s_mux = xSemaphoreCreateMutex();
        if (!s_mux) return ESP_ERR_NO_MEM;
    }

    load_blob_result_t cfg_load = load_blob(NS_CFG, KEY_CFG, &s_cfg, sizeof(s_cfg));
    if (cfg_load == LOAD_BLOB_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config; keeping compile-time defaults");
    } else if (cfg_load == LOAD_BLOB_TRUNCATED) {
        ESP_LOGW(TAG, "Config blob is truncated or oversized; defaults retained for missing fields");
    } else if (cfg_load == LOAD_BLOB_ERROR) {
        ESP_LOGW(TAG, "Config blob could not be read; keeping compile-time defaults");
    }

    // Mileage/trip stats are no longer persisted (see s_stat declaration); stay {0} and start fresh each boot.
    {   // Chart alarm thresholds: load if present in NVS; otherwise keep static defaults (don't overwrite to 0).
        load_blob(NS_CFG, KEY_CHART_ALARM, s_chart_alarm, sizeof(s_chart_alarm));
    }
    {   // Multi-gauge boot animation settings: same as above, load if present else keep defaults.
        load_blob_result_t mg_load = load_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
        if (s_mg.device_position < 1 || s_mg.device_position > 3) s_mg.device_position = 1;
        if (s_mg.intro_enable > 2) s_mg.intro_enable = 2;   // legacy REI/SHINJI/ASUKA (3/4) map to VIDEO (2)
        if (s_mg.boot_mode > 2) s_mg.boot_mode = 0;
        ESP_LOGD("nvs", "mg loaded: intro=%u pos=%u boot=%u (load=%d)", s_mg.intro_enable, s_mg.device_position, s_mg.boot_mode, (int)mg_load);
    }

    /* ---- Config version migration ----
       NOTE: this version migration currently only covers the separate s_mg (KEY_MG_EXTRA) blob.
       nvs_user_cfg_t (s_cfg) uses a different mechanism — the generic grow logic in load_blob():
       if the stored blob is smaller than the current struct, copy the old bytes, keep compile-time
       defaults for the new trailing fields, then rewrite the blob at the new size. That logic REQUIRES
       new fields to be appended at the END of nvs_user_cfg_t, otherwise old data would be reinterpreted
       into the wrong fields. Respect this constraint when adding fields to s_cfg; never insert in the middle. */
    {
        nvs_handle_t h;
        uint8_t stored_ver = 0;
        bool has_ver = false;
        if (nvs_open(NS_CFG, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u8(h, KEY_CFG_VERSION, &stored_ver) == ESP_OK) has_ver = true;
            nvs_close(h);
        }
        if (!has_ver || stored_ver < CFG_VERSION_CURRENT) {
            ESP_LOGW("nvs", "Config migration v%u → v%u", stored_ver, CFG_VERSION_CURRENT);
            // v0 → v1: boot_mode field added, default 0 (SKY GAUGE)
            if (stored_ver < 1) {
                s_mg.boot_mode = 0;
                save_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
            }
            // v1 → v2: theme_cfg.theme becomes a real theme selector (see ui_theme.c).
            // The old default value 1 was never used; reset it to 0 (DEFAULT) so existing
            // devices keep their original look. Must persist, otherwise the next boot would
            // read 1 back from NVS. The version bump means a later deliberate AMBER (index 1)
            // selection won't be reset again.
            if (stored_ver < 2) {
                if (s_cfg.theme_cfg.theme == 1) {
                    s_cfg.theme_cfg.theme = 0;
                    save_blob(NS_CFG, KEY_CFG, &s_cfg, sizeof(s_cfg));
                }
            }
            // v2 → v3: the three built-in boot videos (REI/SHINJI/ASUKA = intro 2/3/4) were
            // replaced by a single app-flashed animation (VIDEO = 2). Old 3/4 map to 2; old 2
            // keeps its value but now plays /bootmedia/boot_block.*. Persist so the roller
            // never shows a stale selection again.
            if (stored_ver < 3) {
                if (s_mg.intro_enable > 2) {
                    s_mg.intro_enable = 2;
                }
                save_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
            }
            // v3 → v4: align the built-in TEMP/INFO defaults with the
            // simulator layout. Preserve deliberate user mappings; only the
            // previous untouched defaults are rewritten.
            if (stored_ver < 4) {
                static const uint8_t old_temp[3] = {0, 1, 2};
                static const uint8_t old_info[5] = {0, 2, 3, 4, 1};
                if (memcmp(s_cfg.temp_display_map, old_temp, sizeof(old_temp)) == 0) {
                    memcpy(s_cfg.temp_display_map, (uint8_t[]){0, 2, 1}, 3);
                }
                if (memcmp(s_cfg.info_display_map, old_info, sizeof(old_info)) == 0) {
                    memcpy(s_cfg.info_display_map, (uint8_t[]){5, 6, 0, 7, 1}, 5);
                }
                save_blob(NS_CFG, KEY_CFG, &s_cfg, sizeof(s_cfg));
            }
            // Write the new version number
            if (nvs_open(NS_CFG, NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_u8(h, KEY_CFG_VERSION, CFG_VERSION_CURRENT);
                nvs_commit(h);
                nvs_close(h);
            }
            ESP_LOGI("nvs", "Config migration done, now v%u", CFG_VERSION_CURRENT);
        }
    }

    /* Default-value repair for new fields (old NVS data has rsv[x] all zero) */
    if(s_cfg.brightness_day < 10) s_cfg.brightness_day = 100; // valid range 10-100; 0/unset/out-of-range all become 100
    if(s_cfg.default_page >= NVS_DEFAULT_PAGE_COUNT) s_cfg.default_page = 0;
    if(s_cfg.needle_source_idx >= DISP_ITEM_COUNT) s_cfg.needle_source_idx = 0;
    if(s_cfg.device_role > 2) s_cfg.device_role = ESPNOW_ROLE_STANDALONE; // role: 0=master 1=slave 2=standalone; out-of-range -> standalone
    if(s_cfg.chart_source_idx >= DISP_ITEM_COUNT) s_cfg.chart_source_idx = DISP_ITEM_OILP;
    // Clamp vehicle profile index to the registered profile count (out-of-range -> index 0)
    uint8_t vehicle_count = 0;
    vehicle_profile_get_all(&vehicle_count);
    if(vehicle_count > 0 && s_cfg.vehicle_profile_idx >= vehicle_count) s_cfg.vehicle_profile_idx = 0;
    if(s_cfg.brake_temp_warn_c < 10 || s_cfg.brake_temp_warn_c > 1200) s_cfg.brake_temp_warn_c = 600;
    if(s_cfg.oil_pressure_warn_x10 > 100) s_cfg.oil_pressure_warn_x10 = 80;
    // 0=unset/legacy out-of-range -> default 6000; clamped here centrally so callers (ui.c / ui_ScreenPageRpmWarn.c) don't repeat the check
    if(s_cfg.rpm_warn_threshold < 1000) s_cfg.rpm_warn_threshold = 6000;
    // TPMS limits are appended fields, so zero values identify old NVS blobs.
    // Keep them in a useful range even if a partially written/invalid blob is found.
    if (s_cfg.tpms_pressure_min_bar_x100 < 100 ||
        s_cfg.tpms_pressure_min_bar_x100 > 400) {
        s_cfg.tpms_pressure_min_bar_x100 = 200;
    }
    if (s_cfg.tpms_pressure_max_bar_x100 < 100 ||
        s_cfg.tpms_pressure_max_bar_x100 > 400 ||
        s_cfg.tpms_pressure_max_bar_x100 <= s_cfg.tpms_pressure_min_bar_x100) {
        s_cfg.tpms_pressure_max_bar_x100 = 320;
        if (s_cfg.tpms_pressure_max_bar_x100 <= s_cfg.tpms_pressure_min_bar_x100)
            s_cfg.tpms_pressure_min_bar_x100 = 200;
    }
    if (s_cfg.tpms_voltage_min_mv < 10000 || s_cfg.tpms_voltage_min_mv > 15000)
        s_cfg.tpms_voltage_min_mv = 12000;

    // Validate TEMP/INFO custom display-item maps: 0..(DISP_ITEM_COUNT-1)
    for (int i = 0; i < 3; ++i) {
        if (s_cfg.temp_display_map[i] >= DISP_ITEM_COUNT) s_cfg.temp_display_map[i] = (uint8_t)i;
    }
    for (int i = 0; i < 5; ++i) {
        if (s_cfg.info_display_map[i] >= DISP_ITEM_COUNT) {
            static const uint8_t def_map[5] = {0, 2, 3, 4, 1};
            s_cfg.info_display_map[i] = def_map[i];
        }
    }

    cfg_normalize(&s_cfg);
    return ESP_OK;
}

/* User config */
const nvs_user_cfg_t * nvs_cfg_get(void){ return &s_cfg; }

esp_err_t nvs_cfg_get_snapshot(nvs_user_cfg_t *out)
{
    if (!out || !s_mux) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mux, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    *out = s_cfg;
    xSemaphoreGive(s_mux);
    return ESP_OK;
}

esp_err_t nvs_cfg_set(const nvs_user_cfg_t *cfg)
{
    if(!cfg) return ESP_ERR_INVALID_ARG;
    nvs_user_cfg_t next = *cfg;
    if (!cfg_validate(&next)) return ESP_ERR_INVALID_ARG;
    if (!s_mux) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mux, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    if(memcmp(&next, &s_cfg, sizeof(s_cfg))==0) {
        xSemaphoreGive(s_mux);
        return ESP_OK;
    }
    esp_err_t err = save_blob(NS_CFG, KEY_CFG, &next, sizeof(next));
    if (err == ESP_OK) s_cfg = next;
    xSemaphoreGive(s_mux);
    return err;
}

/* Chart alarm thresholds */
int16_t nvs_chart_alarm_get(uint8_t item){
    return (item < CHART_ALARM_N) ? s_chart_alarm[item] : CHART_ALARM_OFF;
}
void nvs_chart_alarm_set(uint8_t item, int16_t raw_threshold){
    if(item >= CHART_ALARM_N) return;
    if(s_chart_alarm[item] == raw_threshold) return;
    s_chart_alarm[item] = raw_threshold;
    save_blob(NS_CFG, KEY_CHART_ALARM, s_chart_alarm, sizeof(s_chart_alarm));
}

/* Multi-gauge boot animation settings */
uint8_t nvs_intro_enable_get(void){ return s_mg.intro_enable; }
void nvs_intro_enable_set(uint8_t en){
    if(en >= INTRO_MODE_COUNT) return;
    if(s_mg.intro_enable == en) return;
    s_mg.intro_enable = en;
    save_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
}
uint8_t nvs_device_position_get(void){ return s_mg.device_position; }
void nvs_device_position_set(uint8_t pos){
    if(pos < 1 || pos > 3) return;
    if(s_mg.device_position == pos) return;
    s_mg.device_position = pos;
    save_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
}
uint8_t nvs_boot_mode_get(void){ return s_mg.boot_mode; }
void nvs_boot_mode_set(uint8_t mode){
    if(mode > 2) return;
    if(s_mg.boot_mode == mode) return;
    s_mg.boot_mode = mode;
    save_blob(NS_CFG, KEY_MG_EXTRA, &s_mg, sizeof(s_mg));
}

/* Statistics */
const nvs_stat_t * nvs_stat_get(void){return &s_stat;}
/*
 * Update driving statistics.
 * @param speed_kmh speed in km/h
 * @param dt_ms elapsed time in ms
 * @note skipped if speed is 0, or dt_ms < 1000
 */
void nvs_stat_update_speed(uint8_t speed_kmh, uint32_t dt_ms)
{
    if(dt_ms<1000) return;
    if(speed_kmh == 0) return;

    xSemaphoreTake(s_mux,portMAX_DELAY);
    /* 1. distance = v(km/h)*dt(ms)/3.6  (m) */
    double dist_m = ((double)speed_kmh * (double)dt_ms) / 3.6e3;
    s_stat.odometer_m += (uint64_t)dist_m;
    s_stat.trip_m     += (uint64_t)dist_m;

    /* 2. time */
    s_stat.run_time_s += dt_ms/1000;
    s_stat.trip_run_time_s += dt_ms/1000;

    /* 3. max speed */
    if(speed_kmh > s_stat.max_speed_kmh) s_stat.max_speed_kmh = speed_kmh;

    /* 4. avg speed = trip distance / trip time (m/s) -> km/h */
    if(s_stat.trip_run_time_s){
        double avg_ms = (double)s_stat.trip_m / (double)s_stat.trip_run_time_s; // m/s
        s_stat.avg_speed_kmh = (uint16_t)(avg_ms * 3.6 + 0.5);
        if(s_stat.avg_speed_kmh > s_stat.max_speed_kmh) s_stat.avg_speed_kmh = s_stat.max_speed_kmh;
    }

    xSemaphoreGive(s_mux);
}

/*
 * Reset current-trip statistics (trip distance, max speed, avg speed, running time).
*/
void nvs_stat_reset_trip(void){
    xSemaphoreTake(s_mux,portMAX_DELAY);
    s_stat.trip_m=0;
    s_stat.max_speed_kmh=0;
    s_stat.avg_speed_kmh=0;
    s_stat.run_time_s=0;
    s_stat.trip_run_time_s=0;
    xSemaphoreGive(s_mux);
}

/*
 * Get current-trip statistics (distance, max speed, avg speed, running time).
 * @return a snapshot of the statistics struct
*/
nvs_stat_t nvs_stat_get_mileage(void){
    xSemaphoreTake(s_mux,portMAX_DELAY);
    nvs_stat_t stat = s_stat;
    xSemaphoreGive(s_mux);
    return stat;
}

/* Helpers */
static load_blob_result_t load_blob(const char *ns,const char *key,void *out,size_t len)
{
    nvs_handle_t h; esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            save_blob(ns, key, out, len);
            return LOAD_BLOB_NOT_FOUND;
        }
        return LOAD_BLOB_ERROR;
    }
    {
        // Query the actual stored length first
        size_t stored_len = 0;
        err = nvs_get_blob(h, key, NULL, &stored_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(h);
            save_blob(ns, key, out, len);
            return LOAD_BLOB_NOT_FOUND;
        }
        if (err != ESP_OK || stored_len == 0) {
            nvs_close(h);
            return LOAD_BLOB_ERROR;
        }
        size_t copy_len = (stored_len < len) ? stored_len : len;
        err = nvs_get_blob(h, key, out, &copy_len);
        nvs_close(h);
        if (err != ESP_OK) return LOAD_BLOB_ERROR;
        if (stored_len != len) {
            /* Smaller blobs grow from the caller's defaults; oversized blobs are
             * safely truncated to the current ABI and rewritten. */
            save_blob(ns, key, out, len);
            return LOAD_BLOB_TRUNCATED;
        }
        return LOAD_BLOB_FOUND;
    }
}

static void cfg_normalize(nvs_user_cfg_t *cfg)
{
    if (!cfg) return;
    cfg->ble_device_name[sizeof(cfg->ble_device_name) - 1] = '\0';
    if (cfg->brightness_day < 10 || cfg->brightness_day > 100) cfg->brightness_day = 100;
    if (cfg->default_page >= NVS_DEFAULT_PAGE_COUNT) cfg->default_page = 0;
    if (cfg->needle_source_idx >= DISP_ITEM_COUNT) cfg->needle_source_idx = DISP_ITEM_CLT;
    if (cfg->chart_source_idx >= DISP_ITEM_COUNT) cfg->chart_source_idx = DISP_ITEM_OILP;
    if (cfg->device_role > ESPNOW_ROLE_STANDALONE) cfg->device_role = ESPNOW_ROLE_STANDALONE;
    if (cfg->rpm_warn_anim_en > 1) cfg->rpm_warn_anim_en = 0;
    if (cfg->rpm_warn_linked_en > 1) cfg->rpm_warn_linked_en = 0;
    if (cfg->rc_enabled > 1) cfg->rc_enabled = 0;
    if (cfg->brake_temp_warn_c < 10 || cfg->brake_temp_warn_c > 1200) cfg->brake_temp_warn_c = 600;
    if (cfg->oil_pressure_warn_x10 > 100) cfg->oil_pressure_warn_x10 = 80;
    if (cfg->rpm_warn_threshold < 1000) cfg->rpm_warn_threshold = 6000;
    if (cfg->tpms_pressure_min_bar_x100 < 100 || cfg->tpms_pressure_min_bar_x100 > 400) cfg->tpms_pressure_min_bar_x100 = 200;
    if (cfg->tpms_pressure_max_bar_x100 < 100 || cfg->tpms_pressure_max_bar_x100 > 400 || cfg->tpms_pressure_max_bar_x100 <= cfg->tpms_pressure_min_bar_x100) cfg->tpms_pressure_max_bar_x100 = 320;
    if (cfg->tpms_voltage_min_mv < 10000 || cfg->tpms_voltage_min_mv > 15000) cfg->tpms_voltage_min_mv = 12000;
    for (size_t i = 0; i < 3; ++i) if (cfg->temp_display_map[i] >= DISP_ITEM_COUNT) cfg->temp_display_map[i] = (uint8_t)i;
    static const uint8_t default_info_map[5] = {0, 2, 3, 4, 1};
    for (size_t i = 0; i < 5; ++i) if (cfg->info_display_map[i] >= DISP_ITEM_COUNT) cfg->info_display_map[i] = default_info_map[i];
}

static bool cfg_validate(const nvs_user_cfg_t *cfg)
{
    uint8_t vehicle_count = 0;
    vehicle_profile_get_all(&vehicle_count);
    if (!cfg || cfg->ble_device_name[sizeof(cfg->ble_device_name) - 1] != '\0' ||
        cfg->protocol > 9 ||
        cfg->default_page >= NVS_DEFAULT_PAGE_COUNT || cfg->brightness_day < 10 || cfg->brightness_day > 100 ||
        cfg->needle_source_idx >= DISP_ITEM_COUNT || cfg->chart_source_idx >= DISP_ITEM_COUNT ||
        cfg->device_role > ESPNOW_ROLE_STANDALONE || cfg->rpm_warn_anim_en > 1 ||
        cfg->rpm_warn_linked_en > 1 || cfg->rc_enabled > 1 ||
        cfg->brake_temp_warn_c < 10 || cfg->brake_temp_warn_c > 1200 ||
        cfg->oil_pressure_warn_x10 > 100 || cfg->rpm_warn_threshold < 1000 ||
        cfg->tpms_pressure_min_bar_x100 < 100 || cfg->tpms_pressure_min_bar_x100 > 400 ||
        cfg->tpms_pressure_max_bar_x100 < 100 || cfg->tpms_pressure_max_bar_x100 > 400 ||
        cfg->tpms_pressure_max_bar_x100 <= cfg->tpms_pressure_min_bar_x100 ||
        cfg->tpms_voltage_min_mv < 10000 || cfg->tpms_voltage_min_mv > 15000 ||
        (vehicle_count > 0 && cfg->vehicle_profile_idx >= vehicle_count)) return false;
    for (size_t i = 0; i < 3; ++i) if (cfg->temp_display_map[i] >= DISP_ITEM_COUNT) return false;
    for (size_t i = 0; i < 5; ++i) if (cfg->info_display_map[i] >= DISP_ITEM_COUNT) return false;
    return true;
}

static esp_err_t save_blob(const char *ns,const char *key,const void *data,size_t len)
{
    nvs_handle_t h; esp_err_t err=nvs_open(ns,NVS_READWRITE,&h);
    if(err!=ESP_OK) return err;
    err=nvs_set_blob(h,key,data,len);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err;
}
