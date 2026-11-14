#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "led_strip.h"
#include "nvs_flash.h"

#include "board_audio.h"
#include "music.h"
#include "stream.h"

#define LED_GPIO 38
#define LED_COUNT 7

#define I2C_SDA_GPIO 11
#define I2C_SCL_GPIO 10
#define TCA9555_ADDR 0x20

#define TCA9555_INPUT_PORT_1 0x01

#define K1_MASK (1U << 1)

static const char *TAG = "STATUS_DEMO";

static led_strip_handle_t led_strip;
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t tca9555;

static atomic_bool wifi_connected = false;
#ifdef CONFIG_MVP_ENABLE_WEB
static const bool web_enabled = true;
#else
static const bool web_enabled = false;
#endif
static void start_web_server(void);

static void set_leds(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, g, r, b));
    }
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void leds_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

static void buttons_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9555_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_config, &tca9555));

    esp_err_t result = i2c_master_probe(i2c_bus, TCA9555_ADDR, 50);
    ESP_LOGI(TAG, "TCA9555 0x20: %s", esp_err_to_name(result));
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;background:#111;color:#eee;text-align:center;padding:30px}"
        "button,a{display:block;margin:14px auto;padding:18px;width:240px;border-radius:12px;background:#333;color:white;text-decoration:none;font-size:22px;border:0}"
        "#result{margin-top:20px;color:#8f8;font-size:18px}"
        "</style>"
        "<script>"
        "async function cmd(path){"
        "let r=await fetch(path);"
        "let t=await r.text();"
        "document.getElementById('result').innerText=t;"
        "}"
        "</script></head><body>"
        "<h1>ESP32-S3 AUDIO BOARD</h1>"
        "<p>Web panel is working</p>"
        "<button onclick=\"cmd('/red')\">RED</button>"
        "<button onclick=\"cmd('/green')\">GREEN</button>"
        "<button onclick=\"cmd('/blue')\">BLUE</button>"
        "<button onclick=\"cmd('/status')\">STATUS</button>"
        "<a href='/ota'>OTA UPDATE</a>"
        "<div id='result'>Ready</div>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started. Size: %d bytes", req->content_len);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA update partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buffer[1024];
    int remaining = req->content_len;

    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, remaining > sizeof(buffer) ? sizeof(buffer) : remaining);

        if (received <= 0) {
            ESP_LOGE(TAG, "OTA receive failed");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload successful. Rebooting...");
    httpd_resp_sendstr(req, "OTA upload successful. Rebooting in 2 seconds...");
    xTaskCreate(reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t empty_icon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t red_get_handler(httpd_req_t *req)
{
    set_leds(30, 0, 0);
    httpd_resp_sendstr(req, "RED");
    return ESP_OK;
}

static esp_err_t green_get_handler(httpd_req_t *req)
{
    set_leds(0, 30, 0);
    httpd_resp_sendstr(req, "GREEN");
    return ESP_OK;
}

static esp_err_t blue_get_handler(httpd_req_t *req)
{
    set_leds(0, 0, 30);
    httpd_resp_sendstr(req, "BLUE");
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, wifi_connected ? "WiFi CONNECTED" : "WiFi NOT CONNECTED");
    return ESP_OK;
}

static esp_err_t ota_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;background:#111;color:#eee;text-align:center;padding:30px}"
        "a{display:block;margin:20px auto;color:white;font-size:22px}</style>"
        "</head><body>"
        "<h1>OTA UPDATE</h1>"
        "<p>Select build/esp32_ai_translator.bin</p>"
        "<form method='POST' action='/ota' enctype='application/octet-stream'>"
        "<input type='file' name='firmware' style='font-size:18px;margin:20px'>"
        "<button type='submit' style='font-size:22px;padding:16px'>UPLOAD</button>"
        "</form>"
        "<a href='/'>BACK</a>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void start_web_server(void)
{
    static httpd_handle_t server = NULL;

    if (server != NULL) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;

    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=root_get_handler};
    httpd_uri_t red = {.uri="/red", .method=HTTP_GET, .handler=red_get_handler};
    httpd_uri_t green = {.uri="/green", .method=HTTP_GET, .handler=green_get_handler};
    httpd_uri_t blue = {.uri="/blue", .method=HTTP_GET, .handler=blue_get_handler};
    httpd_uri_t status = {.uri="/status", .method=HTTP_GET, .handler=status_get_handler};
    httpd_uri_t ota_page = {.uri="/ota", .method=HTTP_GET, .handler=ota_get_handler};
    httpd_uri_t ota_upload = {.uri="/ota", .method=HTTP_POST, .handler=ota_post_handler};
    httpd_uri_t favicon_uri = {.uri="/favicon.ico", .method=HTTP_GET, .handler=empty_icon_handler};
    httpd_uri_t apple_icon_uri = {.uri="/apple-touch-icon.png", .method=HTTP_GET, .handler=empty_icon_handler};
    httpd_uri_t apple_icon_pre_uri = {.uri="/apple-touch-icon-precomposed.png", .method=HTTP_GET, .handler=empty_icon_handler};


    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &red));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &green));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &blue));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_page));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_upload));
    httpd_register_uri_handler(server, &favicon_uri);
    httpd_register_uri_handler(server, &apple_icon_uri);
    httpd_register_uri_handler(server, &apple_icon_pre_uri);


    ESP_LOGI(TAG, "HTTP server started on port 80");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi start, connecting...");
        esp_wifi_connect();
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected");
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGE(TAG, "WiFi disconnected");
        esp_wifi_connect();
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        wifi_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (web_enabled) {
            start_web_server();
        }
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_MVP_WIFI_SSID,
            .password = CONFIG_MVP_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

typedef enum {
    DIAGNOSTICS,
    MUSIC,
    ES_TO_RU,
    RU_TO_ES,
    MODE_COUNT
} mode_t_mvp;

static void show_mode(mode_t_mvp mode)
{
    static const uint8_t colors[MODE_COUNT][3] = {
        [DIAGNOSTICS] = {12, 12, 12},
        [MUSIC] = {0, 24, 0},
        [ES_TO_RU] = {0, 0, 24},
        [RU_TO_ES] = {20, 0, 20},
    };
    set_leds(colors[mode][0], colors[mode][1], colors[mode][2]);
    static const char *names[] = {"diagnostics", "music", "ES -> RU", "RU -> ES"};
    ESP_LOGI(TAG, "Mode: %s", names[mode]);
}

typedef enum {
    NO_PRESS,
    SHORT_PRESS,
    LONG_PRESS,
    EXTRA_LONG_PRESS
} button_action_t;

static button_action_t read_k1(void)
{
    static bool candidate, stable, armed;
    static TickType_t changed, down;
    uint8_t port;
    const uint8_t reg = TCA9555_INPUT_PORT_1;
    if (i2c_master_transmit_receive(tca9555, &reg, 1, &port, 1, 30) != ESP_OK) {
        return NO_PRESS;
    }
    bool pressed = (port & K1_MASK) == 0;
    TickType_t now = xTaskGetTickCount();
    if (pressed != candidate) {
        candidate = pressed;
        changed = now;
    }
    if (now - changed < pdMS_TO_TICKS(35)) {
        return NO_PRESS;
    }
    if (!stable && !pressed) {
        armed = true;
    }
    if (stable == pressed) {
        return NO_PRESS;
    }
    stable = pressed;
    if (pressed) {
        down = changed;
        return NO_PRESS;
    }
    if (!armed) {
        return NO_PRESS;
    }
    TickType_t held = changed - down;
    ESP_LOGI(TAG, "K1 held %lu ms", (unsigned long)(held * portTICK_PERIOD_MS));
    if (held >= pdMS_TO_TICKS(2500)) {
        return EXTRA_LONG_PRESS;
    }
    return held >= pdMS_TO_TICKS(700) ? LONG_PRESS : SHORT_PRESS;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    if (esp_ota_get_running_partition()) {
        esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "OTA validity: %s", esp_err_to_name(result));
        }
    }

    leds_init();
    buttons_init();
    board_audio_init(i2c_bus, tca9555);
    bool has_tracks = music_scan();
    if (CONFIG_MVP_WIFI_SSID[0]) {
        wifi_init();
    } else {
        ESP_LOGI(TAG, "Wi-Fi disabled; set SSID through menuconfig");
    }

    mode_t_mvp mode = MUSIC;
    show_mode(mode);
    if (has_tracks) {
        ESP_LOGI(TAG, "Autoplay from microSD");
    }
    bool autoplay_error = has_tracks && !music_select(false);
    TickType_t error_at = 0;
    bool showing_error = false;
    while (true) {
        button_action_t action = read_k1();
        bool ok = !autoplay_error;
        autoplay_error = false;
        if (action == EXTRA_LONG_PRESS) {
            stream_stop();
            music_stop();
            mode = (mode + 1) % MODE_COUNT;
            showing_error = false;
            show_mode(mode);
        } else if (action == SHORT_PRESS) {
            switch (mode) {
            case DIAGNOSTICS:
                board_diagnostics();
                music_scan();
                break;
            case MUSIC:
                if (music_active()) {
                    music_stop();
                } else {
                    ok = music_select(false);
                }
                break;
            case ES_TO_RU:
            case RU_TO_ES:
                if (stream_active()) {
                    stream_finish();
                } else {
                    ok = wifi_connected && stream_start(mode == ES_TO_RU);
                }
                break;
            default:
                break;
            }
        } else if (action == LONG_PRESS) {
            if (mode == MUSIC) {
                ok = music_select(true);
            } else if (stream_active()) {
                stream_stop();
            } else {
                ESP_LOGI(TAG, "Wi-Fi %s", wifi_connected ? "connected" : "offline");
            }
        }
        if (!music_tick() || !stream_tick() || !ok) {
            set_leds(24, 0, 0);
            showing_error = true;
            error_at = xTaskGetTickCount();
        }
        if (showing_error && xTaskGetTickCount() - error_at >= pdMS_TO_TICKS(900)) {
            showing_error = false;
            show_mode(mode);
        }
        // Decoding and capture already pace themselves through I2S DMA.
        if (!music_active() && !stream_active()) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            vTaskDelay(1);
        }
    }
}
