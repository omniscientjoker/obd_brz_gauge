#include "app_media/es8311_audio.h"

#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"
#include "sdkconfig.h"

static const char *TAG = "es8311_audio";

#define AUDIO_I2S_PORT I2S_NUM_0
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_I2S_MCLK GPIO_NUM_2
#define AUDIO_I2S_BCLK GPIO_NUM_48
#define AUDIO_I2S_LRCK GPIO_NUM_38
#define AUDIO_I2S_DOUT GPIO_NUM_47
#define AUDIO_I2S_DIN GPIO_NUM_39 /* RX reserved for a future record path */
#define AUDIO_PA_GPIO GPIO_NUM_9

static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static bool s_ready;

esp_err_t es8311_audio_init(void)
{
#if !CONFIG_OBD_HW_VERSION_V1_WAVESHARE
    ESP_LOGW(TAG, "ES8311 pins overlap the selected LCD board; audio disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_ready) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S channel allocation failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                          I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_LRCK,
            .dout = AUDIO_I2S_DOUT,
            .din = AUDIO_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    i2s_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &i2s_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S standard mode init failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }
    /* The codec data interface does not enable the channel on open.  The
     * Waveshare BSP enables TX explicitly before any codec writes. */
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX channel enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    i2c_master_bus_handle_t bus = I2C_GetBusHandle();
    if (!bus) {
        ESP_LOGW(TAG, "Shared I2C bus is not initialized");
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
        .clock_speed_hz = 400000,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_data_if_t *data_if = NULL;
    const audio_codec_gpio_if_t *gpio_if = NULL;
    if (ctrl_if) {
        audio_codec_i2s_cfg_t data_cfg = {
            .port = AUDIO_I2S_PORT,
            .rx_handle = NULL,
            .tx_handle = s_tx,
        };
        data_if = audio_codec_new_i2s_data(&data_cfg);
        gpio_if = audio_codec_new_gpio();
    }
    if (!ctrl_if || !data_if || !gpio_if) {
        ESP_LOGW(TAG, "ES8311 control/data interface allocation failed");
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }
    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = AUDIO_PA_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
        .no_dac_ref = true,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_cfg);
    if (!codec_if) {
        ESP_LOGW(TAG, "ES8311 not detected at I2C address 0x30");
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) {
        ESP_LOGW(TAG, "ES8311 device allocation failed");
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    if (esp_codec_dev_open(s_codec, &sample) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_codec, 70) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "ES8311 open failed");
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return ESP_FAIL;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ES8311 audio ready: I2S0 16 kHz stereo, MCLK=%d BCLK=%d LRCK=%d DOUT=%d PA=%d",
             AUDIO_I2S_MCLK, AUDIO_I2S_BCLK, AUDIO_I2S_LRCK, AUDIO_I2S_DOUT, AUDIO_PA_GPIO);
    return ESP_OK;
#endif
}

bool es8311_audio_is_ready(void)
{
    return s_ready;
}

esp_err_t es8311_audio_write(const void *data, size_t bytes)
{
    if (!s_ready || !s_codec || !data || bytes == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_write(s_codec, (void *)data, (int)bytes);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t es8311_audio_set_volume(uint8_t volume_percent)
{
    if (!s_ready || !s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    return esp_codec_dev_set_out_vol(s_codec, volume_percent) == ESP_CODEC_DEV_OK ?
           ESP_OK : ESP_FAIL;
}
