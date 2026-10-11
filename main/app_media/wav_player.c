#include "app_media/wav_player.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_media/es8311_audio.h"
#include "app_media/sd_media_manager.h"

static const char *TAG = "wav_player";

#define WAV_QUEUE_DEPTH 4
#define WAV_PATH_MAX 96
#define WAV_READ_BYTES 2048

typedef struct {
    char path[WAV_PATH_MAX];
    uint32_t generation;
} wav_job_t;

typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
} wav_riff_t;

typedef struct {
    char id[4];
    uint32_t size;
} wav_chunk_header_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_playing;
static volatile uint32_t s_generation = 1;

static bool read_exact(FILE *fp, void *buf, size_t len)
{
    return fp && buf && len > 0 && fread(buf, 1, len, fp) == len;
}

static bool chunk_id_is(const char id[4], const char expected[4])
{
    return memcmp(id, expected, 4) == 0;
}

static bool wav_find_data(FILE *fp, uint16_t *channels, uint32_t *sample_rate,
                          uint16_t *bits_per_sample, long *data_offset, uint32_t *data_size)
{
    wav_riff_t riff = {0};
    if (!read_exact(fp, &riff, sizeof(riff)) || memcmp(riff.riff, "RIFF", 4) != 0 ||
        memcmp(riff.wave, "WAVE", 4) != 0) {
        return false;
    }
    bool have_fmt = false;
    bool have_data = false;
    uint16_t format = 0;
    while (!feof(fp)) {
        wav_chunk_header_t chunk = {0};
        if (!read_exact(fp, &chunk, sizeof(chunk))) {
            break;
        }
        long payload = ftell(fp);
        if (payload < 0 || chunk.size > (16u * 1024u * 1024u)) {
            return false;
        }
        if (chunk_id_is(chunk.id, "fmt ")) {
            uint8_t fmt[16] = {0};
            size_t take = chunk.size < sizeof(fmt) ? chunk.size : sizeof(fmt);
            if (!read_exact(fp, fmt, take) || take < sizeof(fmt)) {
                return false;
            }
            format = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8);
            *channels = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8);
            *sample_rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                            ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            *bits_per_sample = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8);
            have_fmt = true;
        } else if (chunk_id_is(chunk.id, "data")) {
            *data_offset = payload;
            *data_size = chunk.size;
            have_data = true;
        }
        if (fseek(fp, payload + chunk.size + (chunk.size & 1u), SEEK_SET) != 0) {
            return false;
        }
        if (have_fmt && have_data) {
            break;
        }
    }
    return have_fmt && have_data && format == 1 && *channels >= 1 && *channels <= 2 &&
           *sample_rate == 16000 && *bits_per_sample == 16;
}

static bool job_is_current(const wav_job_t *job)
{
    return job && s_playing && job->generation == s_generation;
}

static void play_file(const wav_job_t *job)
{
    const char *path = job ? job->path : NULL;
    if (!path || !sd_media_is_ready() || !sd_media_lock(1000)) {
        return;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Cannot open %s", path);
        sd_media_unlock();
        return;
    }
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sample_rate = 0;
    uint32_t data_size = 0;
    long data_offset = 0;
    bool valid = wav_find_data(fp, &channels, &sample_rate, &bits, &data_offset, &data_size);
    if (!valid || fseek(fp, data_offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "Unsupported WAV %s (PCM16/16kHz mono or stereo required)", path);
        fclose(fp);
        sd_media_unlock();
        return;
    }
    uint8_t input[WAV_READ_BYTES];
    int16_t stereo[WAV_READ_BYTES];
    uint32_t remaining = data_size;
    while (remaining > 0 && job_is_current(job)) {
        size_t want = remaining < sizeof(input) ? remaining : sizeof(input);
        want -= want % (channels * sizeof(int16_t));
        if (want == 0) {
            break;
        }
        size_t got = fread(input, 1, want, fp);
        if (got != want) {
            ESP_LOGW(TAG, "Short WAV read for %s", path);
            break;
        }
        size_t samples = got / sizeof(int16_t);
        size_t output_samples = channels == 1 ? samples * 2 : samples;
        if (channels == 1) {
            const int16_t *mono = (const int16_t *)input;
            for (size_t i = 0; i < samples; i++) {
                stereo[i * 2] = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
        } else {
            memcpy(stereo, input, got);
        }
        if (es8311_audio_write(stereo, output_samples * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "I2S write failed for %s", path);
            break;
        }
        remaining -= (uint32_t)got;
    }
    fclose(fp);
    sd_media_unlock();
    ESP_LOGD(TAG, "Played %s (%u Hz, %u ch, %u bit)", path,
             (unsigned)sample_rate, (unsigned)channels, (unsigned)bits);
}

static void wav_player_task(void *arg)
{
    (void)arg;
    wav_job_t job;
    while (true) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) == pdTRUE) {
            if (job.generation != s_generation) continue;
            s_playing = true;
            play_file(&job);
            s_playing = false;
        }
    }
}

esp_err_t wav_player_init(void)
{
    if (s_queue) {
        return ESP_OK;
    }
    if (!es8311_audio_is_ready()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_queue = xQueueCreate(WAV_QUEUE_DEPTH, sizeof(wav_job_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(wav_player_task, "wav_player", 6144, NULL,
                    tskIDLE_PRIORITY + 2, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t wav_player_play(const char *path)
{
    if (!s_queue || !path || strlen(path) >= WAV_PATH_MAX ||
        strncmp(path, "/sdcard/", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    wav_job_t job = {0};
    strncpy(job.path, path, sizeof(job.path) - 1);
    job.generation = s_generation;
    return xQueueSend(s_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void wav_player_stop(void)
{
    if (!s_queue) return;
    s_playing = false;
    ++s_generation;
    if (s_generation == 0) ++s_generation;
    xQueueReset(s_queue);
}

esp_err_t wav_player_preview(const char *path)
{
    if (!s_queue || !path || strlen(path) >= WAV_PATH_MAX ||
        strncmp(path, "/sdcard/", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wav_player_stop();
    wav_job_t job = {0};
    strncpy(job.path, path, sizeof(job.path) - 1);
    job.generation = s_generation;
    return xQueueSend(s_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool wav_player_is_playing(void)
{
    return s_playing;
}
