#include "music.h"
#include "board_audio.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

#define MOUNT "/sd"
#define MAX_TRACKS 8192
#define MAX_PATH 512
#define MAX_DEPTH 8
static const char *TAG = "music";
static sdmmc_card_t *card;
static char (*tracks)[MAX_PATH];
static size_t *folders;
static size_t folder_count;
static size_t count, selected;
static FILE *file;
static mp3dec_t decoder;
static uint8_t input[16384];
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static size_t buffered;
static unsigned phase;
static bool played;

static size_t folder_length(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? (size_t)(slash - path) : 0;
}

static int compare_folders(const char *a, const char *b)
{
    size_t alen = folder_length(a), blen = folder_length(b);
    int result = strncmp(a, b, alen < blen ? alen : blen);
    return result ? result : (alen > blen) - (alen < blen);
}

static int compare_names(const void *a, const void *b)
{
    int result = compare_folders(a, b);
    return result ? result : strcmp(a, b);
}

static void scan_folder(char *path, size_t length, unsigned depth)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot scan %s", path);
        return;
    }
    struct dirent *entry;
    while (count < MAX_TRACKS && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        size_t size = strlen(name);
        if (name[0] == '.' || length + 1 + size >= sizeof(MOUNT) + MAX_PATH) {
            continue;
        }
        path[length] = '/';
        memcpy(path + length + 1, name, size + 1);
        if (entry->d_type == DT_DIR && depth < MAX_DEPTH) {
            scan_folder(path, length + 1 + size, depth + 1);
        } else if (entry->d_type == DT_REG && size > 4 &&
                   strcasecmp(name + size - 4, ".mp3") == 0) {
            strcpy(tracks[count++], path + sizeof(MOUNT));
        }
        path[length] = '\0';
    }
    closedir(dir);
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
        const esp_vfs_fat_sdmmc_mount_config_t config = {.max_files = MAX_DEPTH + 2};
        esp_err_t result = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &config, &card);
        ESP_LOGI(TAG, "microSD mount: %s", esp_err_to_name(result));
        if (result != ESP_OK) {
            return false;
        }
    }
    if (!tracks) {
        tracks = heap_caps_malloc(MAX_TRACKS * sizeof(*tracks), MALLOC_CAP_SPIRAM);
        folders = heap_caps_malloc(MAX_TRACKS * sizeof(*folders), MALLOC_CAP_SPIRAM);
        if (!tracks || !folders) {
            free(tracks);
            free(folders);
            tracks = NULL;
            folders = NULL;
            ESP_LOGE(TAG, "Cannot allocate music index in PSRAM");
            return false;
        }
    }
    count = selected = folder_count = 0;
    char path[sizeof(MOUNT) + MAX_PATH] = MOUNT;
    scan_folder(path, sizeof(MOUNT) - 1, 0);
    qsort(tracks, count, sizeof(tracks[0]), compare_names);
    for (size_t i = 0; i < count; ++i) {
        if (i == 0 || compare_folders(tracks[i - 1], tracks[i]) != 0) {
            folders[folder_count++] = i;
        }
    }
    ESP_LOGI(TAG, "MP3 files: %u, folders: %u", (unsigned)count, (unsigned)folder_count);
    if (count == MAX_TRACKS) {
        ESP_LOGW(TAG, "MP3 index reached its limit");
    }
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

static bool play_selected(void)
{
    music_stop();
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

bool music_select(int step)
{
    if (!board_speaker_ready() || (!count && !music_scan())) {
        return false;
    }
    selected = (selected + count + step) % count;
    return play_selected();
}

bool music_select_folder(int direction)
{
    if (!board_speaker_ready() || (!count && !music_scan())) {
        return false;
    }
    if (folder_count <= 1) {
        return true;
    }
    size_t index = 0;
    while (index + 1 < folder_count && folders[index + 1] <= selected) {
        ++index;
    }
    index = (index + folder_count + direction) % folder_count;
    selected = folders[index];
    return play_selected();
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
        if (!played) {
            ESP_LOGI(TAG, "First decoded PCM sent to I2S (%d Hz source, %d channels)",
                     info.hz, info.channels);
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
    if (ok) {
        ESP_LOGI(TAG, "Track completed, advancing");
        return music_select(1);
    } else {
        ESP_LOGE(TAG, "MP3 decode or SD read failed");
    }
    return ok;
}
