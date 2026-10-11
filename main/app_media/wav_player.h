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

/* Stop current playback and replace any queued playback with this preview. */
esp_err_t wav_player_preview(const char *path);

/* Stop current playback and discard queued files. */
void wav_player_stop(void);

bool wav_player_is_playing(void);

#ifdef __cplusplus
}
#endif
