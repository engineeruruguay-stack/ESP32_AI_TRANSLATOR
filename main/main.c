#include <stdbool.h>
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
#include "led_strip.h"
#include "nvs_flash.h"

#define WIFI_SSID "iPhone Aleksandr 16 pro"
#define WIFI_PASS "12345678"

#define LED_GPIO 38
#define LED_COUNT 7

#define I2C_SDA_GPIO 11
#define I2C_SCL_GPIO 10
#define TCA9555_ADDR 0x20

#define TCA9555_INPUT_PORT_1 0x01
#define TCA9555_CONFIG_PORT_1 0x07

#define K1_PIN 9
#define K2_PIN 10
#define K3_PIN 11

static const char *TAG = "STATUS_DEMO";

static led_strip_handle_t led_strip;
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t tca9555;

static bool wifi_connected = false;
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

static void tca9555_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(tca9555, data, sizeof(data), -1));
}

static uint8_t tca9555_read_reg(uint8_t reg)
{
    uint8_t value = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(tca9555, &reg, 1, &value, 1, -1));
    return value;
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

    tca9555_write_reg(TCA9555_CONFIG_PORT_1, 0xFF);
}

static bool button_pressed(uint8_t pin)
{
    uint8_t port1 = tca9555_read_reg(TCA9555_INPUT_PORT_1);
    uint8_t bit = pin - 8;
    return ((port1 & (1 << bit)) == 0);
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
        "<p>OTA page is working.</p>"
        "<p>Firmware upload will be added next step.</p>"
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
    httpd_uri_t favicon_uri = {.uri="/favicon.ico", .method=HTTP_GET, .handler=empty_icon_handler};
    httpd_uri_t apple_icon_uri = {.uri="/apple-touch-icon.png", .method=HTTP_GET, .handler=empty_icon_handler};
    httpd_uri_t apple_icon_pre_uri = {.uri="/apple-touch-icon-precomposed.png", .method=HTTP_GET, .handler=empty_icon_handler};


    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &red));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &green));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &blue));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_page));
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
        set_leds(30, 0, 0);
        esp_wifi_connect();
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        wifi_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        set_leds(0, 30, 0);
        start_web_server();
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
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Unified status demo started");

    leds_init();
    set_leds(0, 0, 30);

    buttons_init();

    ESP_LOGI(TAG, "Boot status: BLUE");
    ESP_LOGI(TAG, "WiFi connecting: blinking YELLOW");
    ESP_LOGI(TAG, "WiFi connected: GREEN");
    ESP_LOGI(TAG, "WiFi error: RED");

    wifi_init();

    bool k1_prev = false;
    bool k2_prev = false;
    bool k3_prev = false;
    int heartbeat = 0;

    while (1) {
        bool k1 = button_pressed(K1_PIN);
        bool k2 = button_pressed(K2_PIN);
        bool k3 = button_pressed(K3_PIN);

        if (k1 && !k1_prev) {
            ESP_LOGI(TAG, "K1: start WiFi reconnect");
            wifi_connected = false;
            set_leds(30, 30, 0);
            esp_wifi_disconnect();
            esp_wifi_connect();
        }

        if (k2 && !k2_prev) {
            ESP_LOGI(TAG, "K2: audio test placeholder");
            set_leds(0, 0, 30);
        }

        if (k3 && !k3_prev) {
            ESP_LOGI(TAG, "K3: status requested. WiFi: %s", wifi_connected ? "CONNECTED" : "NOT CONNECTED");
            if (wifi_connected) {
                set_leds(0, 30, 0);
            } else {
                set_leds(30, 30, 0);
            }
        }

        k1_prev = k1;
        k2_prev = k2;
        k3_prev = k3;

        if (!wifi_connected) {
            set_leds(30, 30, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
            led_strip_clear(led_strip);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if ((heartbeat++ % 10) == 0) {
            ESP_LOGI(TAG, "Heartbeat. WiFi: %s", wifi_connected ? "CONNECTED" : "NOT CONNECTED");
        }
    }
}
