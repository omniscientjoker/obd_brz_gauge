#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the serialized WAV playback task. */
esp_err_t wav_player_init(void);

/* Queue one SD path. The path is copied and the call returns immediately. */
esp_err_t wav_player_play(const char *path);

bool wav_player_is_playing(void);

#ifdef __cplusplus
}
#endif
