#include "music.h"
#include "board_audio.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

#define MOUNT "/sd"
#define MAX_TRACKS 64
static const char *TAG = "music";
static sdmmc_card_t *card;
static char tracks[MAX_TRACKS][256];
static size_t count, selected;
static FILE *file;
static mp3dec_t decoder;
static uint8_t input[16384];
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static size_t buffered;
static unsigned phase;
static bool played;

static int compare_names(const void *a, const void *b)
{
    return strcmp(a, b);
}

bool music_scan(void)
{
    if (!card) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 1;
        slot.clk = 40;
        slot.cmd = 42;
        slot.d0 = 41;
        slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        const esp_vfs_fat_sdmmc_mount_config_t config = {.max_files = 2};
        esp_err_t result = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &config, &card);
        ESP_LOGI(TAG, "microSD mount: %s", esp_err_to_name(result));
        if (result != ESP_OK) {
            return false;
        }
    }
    count = selected = 0;
    DIR *dir = opendir(MOUNT);
    if (!dir) {
        return false;
    }
    struct dirent *entry;
    while (count < MAX_TRACKS && (entry = readdir(dir))) {
        size_t length = strlen(entry->d_name);
        if (entry->d_type == DT_REG && entry->d_name[0] != '.' &&
            length > 4 && length < sizeof(tracks[0]) &&
            strcasecmp(entry->d_name + length - 4, ".mp3") == 0) {
            memcpy(tracks[count++], entry->d_name, length + 1);
        }
    }
    closedir(dir);
    qsort(tracks, count, sizeof(tracks[0]), compare_names);
    ESP_LOGI(TAG, "MP3 files in root: %u", (unsigned)count);
    return count != 0;
}

bool music_active(void) { return file != NULL; }

void music_stop(void)
{
    if (file) {
        fclose(file);
        file = NULL;
        if (!board_amp(false)) {
            ESP_LOGE(TAG, "Speaker mute failed");
        }
    }
}

static bool skip_id3(FILE *track)
{
    uint8_t header[10];
    size_t n = fread(header, 1, sizeof(header), track);
    if (n < sizeof(header) || memcmp(header, "ID3", 3) != 0) {
        return fseek(track, 0, SEEK_SET) == 0;
    }
    if ((header[6] | header[7] | header[8] | header[9]) & 0x80) {
        return false;
    }
    long size = ((long)header[6] << 21) | ((long)header[7] << 14) |
                ((long)header[8] << 7) | header[9];
    return fseek(track, size + ((header[5] & 0x10) ? 10 : 0), SEEK_CUR) == 0;
}

bool music_select(bool next)
{
    music_stop();
    if (!board_speaker_ready() || (!count && !music_scan())) {
        return false;
    }
    if (next) {
        selected = (selected + 1) % count;
    }
    char filename[sizeof(MOUNT) + sizeof(tracks[0])];
    snprintf(filename, sizeof(filename), MOUNT "/%s", tracks[selected]);
    file = fopen(filename, "rb");
    if (!file || !skip_id3(file) || !board_amp(true)) {
        ESP_LOGE(TAG, "Cannot start %s", filename);
        music_stop();
        return false;
    }
    mp3dec_init(&decoder);
    buffered = phase = 0;
    played = false;
    ESP_LOGI(TAG, "Playing %s", filename);
    return true;
}

bool music_tick(void)
{
    if (!file) {
        return true;
    }
    if (buffered < 4096) {
        buffered += fread(input + buffered, 1, sizeof(input) - buffered, file);
    }
    mp3dec_frame_info_t info = {0};
    int samples = mp3dec_decode_frame(&decoder, input, buffered, pcm, &info);
    if (samples > 0) {
        int16_t block[AUDIO_BLOCK];
        size_t used = 0;
        for (int i = 0; i < samples; ++i) {
            // Fixed-rate mono output keeps the shared ADC/DAC clock stable.
            int sample = pcm[i * info.channels];
            if (info.channels == 2) {
                sample = (sample + pcm[i * 2 + 1]) / 2;
            }
            phase += AUDIO_RATE;
            while (phase >= info.hz) {
                phase -= info.hz;
                block[used++] = sample;
                if (used == AUDIO_BLOCK) {
                    if (!board_write(block, used)) {
                        music_stop();
                        return false;
                    }
                    used = 0;
                }
            }
        }
        if (used && !board_write(block, used)) {
            music_stop();
            return false;
        }
        played = true;
    }
    if (info.frame_bytes > 0) {
        buffered -= info.frame_bytes;
        memmove(input, input + info.frame_bytes, buffered);
        return true;
    }
    bool ok = played && !ferror(file);
    if (ok) {
        int16_t silence[AUDIO_BLOCK] = {0};
        for (int i = 0; i < 8 && ok; ++i) {
            ok = board_write(silence, AUDIO_BLOCK);
        }
    }
    music_stop();
    if (!ok) {
        ESP_LOGE(TAG, "MP3 decode or SD read failed");
    }
    return ok;
}
