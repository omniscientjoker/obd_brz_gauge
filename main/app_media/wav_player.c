#include "app_media/wav_player.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "app_media/es8311_audio.h"
#include "app_media/sd_media_manager.h"

static const char *TAG = "wav_player";

#define WAV_QUEUE_DEPTH 4
#define WAV_PATH_MAX 160
#define WAV_READ_BYTES 1024
/* Keep the fixed output chunk small enough for internal DRAM. If a lower-rate
 * input expands past this buffer, play_file flushes it and continues instead
 * of dropping the rest of the input chunk. */
#define WAV_OUTPUT_FRAMES WAV_READ_BYTES
#define WAV_OUTPUT_RATE 44100

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
/* Keep the small streaming buffers in linker-placed internal DRAM. Dynamic
 * DMA allocation happens late in app startup, after LVGL and the display
 * buffers have fragmented the DMA heap, and can fail even with free SRAM. */
static DRAM_ATTR uint8_t s_input_storage[WAV_READ_BYTES] __attribute__((aligned(4)));
static DRAM_ATTR int16_t s_stereo_storage[WAV_OUTPUT_FRAMES * 2] __attribute__((aligned(4)));
static uint8_t *s_input = s_input_storage;
static int16_t *s_stereo = s_stereo_storage;

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
           *sample_rate >= 8000 && *sample_rate <= 96000 && *bits_per_sample == 16;
}

static bool job_is_current(const wav_job_t *job)
{
    return job && s_playing && job->generation == s_generation;
}

static esp_err_t play_file(const wav_job_t *job)
{
    const char *path = job ? job->path : NULL;
    if (!path) {
        ESP_LOGW(TAG, "Playback skipped: invalid path");
        return ESP_ERR_INVALID_ARG;
    }
    if (!sd_media_is_ready()) {
        ESP_LOGW(TAG, "Playback skipped: SD card is not ready (%s)", path);
        return ESP_ERR_NOT_FOUND;
    }
    if (!sd_media_lock(1000)) {
        ESP_LOGW(TAG, "Playback skipped: SD card lock timeout (%s)", path);
        return ESP_ERR_TIMEOUT;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Cannot open %s", path);
        sd_media_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sample_rate = 0;
    uint32_t data_size = 0;
    long data_offset = 0;
    bool valid = wav_find_data(fp, &channels, &sample_rate, &bits, &data_offset, &data_size);
    if (!valid || fseek(fp, data_offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "Unsupported WAV %s (PCM16, 8-96kHz, mono or stereo required)", path);
        fclose(fp);
        sd_media_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *input = s_input;
    int16_t *stereo = s_stereo;
    if (!input || !stereo) {
        ESP_LOGE(TAG, "Audio buffers are not initialized");
        fclose(fp);
        sd_media_unlock();
        return ESP_ERR_NO_MEM;
    }
    uint32_t remaining = data_size;
    uint32_t rate_accum = 0;
    esp_err_t result = ESP_OK;
    while (remaining > 0 && job_is_current(job)) {
        const size_t frame_bytes = channels * sizeof(int16_t);
        size_t want = remaining < WAV_READ_BYTES ? remaining : WAV_READ_BYTES;
        want -= want % frame_bytes;
        if (want == 0) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        size_t got = fread(input, 1, want, fp);
        if (got != want) {
            ESP_LOGW(TAG, "Short WAV read for %s", path);
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        const int16_t *pcm = (const int16_t *)input;
        const size_t input_frames = got / frame_bytes;
        size_t output_frames = 0;
        bool write_failed = false;
        for (size_t frame = 0; frame < input_frames; ++frame) {
            rate_accum += WAV_OUTPUT_RATE;
            while (rate_accum >= sample_rate) {
                if (output_frames == WAV_OUTPUT_FRAMES) {
                    if (es8311_audio_write(stereo, output_frames * 2 * sizeof(int16_t)) != ESP_OK) {
                        write_failed = true;
                        result = ESP_FAIL;
                        break;
                    }
                    output_frames = 0;
                }
                const int16_t left = pcm[frame * channels];
                const int16_t right = channels == 2 ? pcm[frame * channels + 1] : left;
                stereo[output_frames * 2] = left;
                stereo[output_frames * 2 + 1] = right;
                ++output_frames;
                rate_accum -= sample_rate;
            }
            if (write_failed) break;
        }
        if (!write_failed && output_frames > 0 &&
            es8311_audio_write(stereo, output_frames * 2 * sizeof(int16_t)) != ESP_OK) {
            write_failed = true;
        }
        if (write_failed) {
            ESP_LOGW(TAG, "I2S write failed for %s", path);
            result = ESP_FAIL;
            break;
        }
        remaining -= (uint32_t)got;
    }
    if (result == ESP_OK && !job_is_current(job)) {
        result = ESP_ERR_INVALID_STATE;
    } else if (result == ESP_OK && remaining != 0) {
        result = ESP_ERR_INVALID_SIZE;
    }
    fclose(fp);
    sd_media_unlock();
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Played %s (%u Hz, %u ch, %u bit)", path,
                 (unsigned)sample_rate, (unsigned)channels, (unsigned)bits);
    } else if (result == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Playback stopped %s", path);
    } else {
        ESP_LOGW(TAG, "Playback failed %s: %s", path, esp_err_to_name(result));
    }
    return result;
}

static void wav_player_task(void *arg)
{
    (void)arg;
    wav_job_t job;
    while (true) {
        /* Keep the queue non-empty until busy is visible. Consumers checking
         * global audio/video exclusion therefore cannot observe a false idle
         * gap between dequeue and playback startup. */
        if (xQueuePeek(s_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        s_playing = true;
        if (xQueueReceive(s_queue, &job, 0) != pdTRUE) {
            s_playing = false;
            continue;
        }
        if (job.generation == s_generation) play_file(&job);
        s_playing = false;
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
    if (xTaskCreateWithCaps(wav_player_task, "wav_player", 6144, NULL,
                            tskIDLE_PRIORITY + 2, &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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
    /* The worker owns this flag until its current I2S write and file cleanup
     * finish. Clearing it here makes callers believe audio is idle while a
     * final chunk may still be in flight (which can overlap video playback). */
    ++s_generation;
    if (s_generation == 0) ++s_generation;
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
    esp_err_t err = xQueueSend(s_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    ESP_LOGI(TAG, "Preview %s: %s", path, esp_err_to_name(err));
    return err;
}

bool wav_player_is_playing(void)
{
    return s_playing;
}

bool wav_player_is_busy(void)
{
    if (!s_queue) return false;
    return s_playing || uxQueueMessagesWaiting(s_queue) != 0;
}
