#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize ES8311 on the existing I2C bus and the I2S TX channel. */
esp_err_t es8311_audio_init(void);
bool es8311_audio_is_ready(void);

/* Audio is fixed to 16-bit, 16 kHz, stereo PCM at the codec boundary. */
esp_err_t es8311_audio_write(const void *data, size_t bytes);
esp_err_t es8311_audio_set_volume(uint8_t volume_percent);

#ifdef __cplusplus
}
#endif
