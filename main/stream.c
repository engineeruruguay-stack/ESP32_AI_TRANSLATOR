#include "stream.h"
#include "board_audio.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

static const char *TAG = "stream";
static int connection = -1;
static bool receiving, amp_on;
static TickType_t started;
static int16_t pcm[AUDIO_BLOCK];
static size_t pending;

static bool send_all(const void *buffer, size_t size)
{
    const uint8_t *bytes = buffer;
    while (size) {
        int sent = send(connection, bytes, size, 0);
        if (sent <= 0) {
            return false;
        }
        bytes += sent;
        size -= sent;
    }
    return true;
}

bool stream_active(void) { return connection >= 0; }

void stream_stop(void)
{
    if (connection >= 0) {
        close(connection);
        connection = -1;
    }
    if (amp_on) {
        board_amp(false);
        amp_on = false;
    }
}

static bool connect_gateway(const struct sockaddr_in *address)
{
    if (fcntl(connection, F_SETFL, O_NONBLOCK) < 0) {
        return false;
    }
    int result = connect(connection, (const struct sockaddr *)address, sizeof(*address));
    if (result < 0) {
        if (errno != EINPROGRESS) {
            return false;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(connection, &writable);
        struct timeval limit = {.tv_sec = 1};
        int pending_error;
        socklen_t size = sizeof(pending_error);
        if (select(connection + 1, NULL, &writable, NULL, &limit) != 1 ||
            getsockopt(connection, SOL_SOCKET, SO_ERROR, &pending_error, &size) != 0 ||
            pending_error != 0) {
            return false;
        }
    }
    return fcntl(connection, F_SETFL, 0) == 0;
}

bool stream_start(bool spanish_source)
{
    if (stream_active() || !board_mic_ready() || !board_speaker_ready()) {
        return false;
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_MVP_GATEWAY_PORT),
    };
    if (inet_pton(AF_INET, CONFIG_MVP_GATEWAY_IP, &address.sin_addr) != 1) {
        ESP_LOGW(TAG, "Configure the test gateway IPv4 address first");
        return false;
    }
    connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection < 0) {
        return false;
    }
    struct timeval timeout = {.tv_usec = 250000};
    if (setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
        !connect_gateway(&address) ||
        !send_all(spanish_source ? "WV1 ES RU\n" : "WV1 RU ES\n", 10)) {
        stream_stop();
        ESP_LOGW(TAG, "Gateway connection failed");
        return false;
    }
    receiving = false;
    started = xTaskGetTickCount();
    pending = 0;
    ESP_LOGI(TAG, "Capture started (%s)", spanish_source ? "ES to RU" : "RU to ES");
    return true;
}

void stream_finish(void)
{
    if (stream_active() && !receiving) {
        shutdown(connection, SHUT_WR);
        receiving = true;
        started = xTaskGetTickCount();
        ESP_LOGI(TAG, "Capture ended, waiting for PCM reply");
    }
}

bool stream_tick(void)
{
    if (!stream_active()) {
        return true;
    }
    TickType_t elapsed = xTaskGetTickCount() - started;
    if (!receiving) {
        if (elapsed >= pdMS_TO_TICKS(15000)) {
            stream_finish();
            return true;
        }
        if (board_read(pcm, AUDIO_BLOCK, false) && send_all(pcm, sizeof(pcm))) {
            return true;
        }
    } else {
        uint8_t *bytes = (uint8_t *)pcm;
        int n = recv(connection, bytes + pending, sizeof(pcm) - pending, MSG_DONTWAIT);
        if (n == 0) {
            bool ok = pending == 0 && amp_on;
            int16_t silence[AUDIO_BLOCK] = {0};
            for (int i = 0; i < 8 && ok; ++i) {
                ok = board_write(silence, AUDIO_BLOCK);
            }
            stream_stop();
            return ok;
        }
        if (n > 0) {
            started = xTaskGetTickCount();
            pending += n;
            size_t frames = pending / sizeof(int16_t);
            if (frames) {
                if (!amp_on) {
                    amp_on = board_amp(true);
                }
                if (!amp_on || !board_write(pcm, frames)) {
                    stream_stop();
                    return false;
                }
                pending %= sizeof(int16_t);
                if (pending) {
                    bytes[0] = bytes[frames * sizeof(int16_t)];
                }
            }
            return true;
        }
        if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
            elapsed < pdMS_TO_TICKS(15000)) {
            return true;
        }
    }
    ESP_LOGW(TAG, "Gateway stream ended with an I/O error or timeout");
    stream_stop();
    return false;
}
