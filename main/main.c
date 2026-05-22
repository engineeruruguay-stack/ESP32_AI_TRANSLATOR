#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

static const char *TAG = "BUTTON_RGB_DEMO";

#define I2C_SCL_GPIO 10
#define I2C_SDA_GPIO 11
#define TCA9555_ADDR 0x20

#define TCA9555_INPUT_PORT_1_REG 0x01
#define TCA9555_CONFIG_PORT_1_REG 0x07

#define KEY_K1_BIT BIT1
#define KEY_K2_BIT BIT2
#define KEY_K3_BIT BIT3
#define KEY_MASK (KEY_K1_BIT | KEY_K2_BIT | KEY_K3_BIT)

#define LED_STRIP_GPIO 38
#define LED_STRIP_COUNT 7

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t tca9555;
static led_strip_handle_t led_strip;

static esp_err_t tca9555_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(tca9555, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t tca9555_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(tca9555, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static void set_all_leds(uint8_t red, uint8_t green, uint8_t blue)
{
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, red, green, blue));
    }
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void init_rgb_leds(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

static void init_tca9555_buttons(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9555_ADDR,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &device_config, &tca9555));

    uint8_t config_port_1 = 0;
    ESP_ERROR_CHECK(tca9555_read_reg(TCA9555_CONFIG_PORT_1_REG, &config_port_1));

    // K1/K2/K3 are active-low inputs on TCA9555 pins 9, 10, and 11.
    config_port_1 |= KEY_MASK;
    ESP_ERROR_CHECK(tca9555_write_reg(TCA9555_CONFIG_PORT_1_REG, config_port_1));
}

void app_main(void)
{
    uint32_t heartbeat = 0;
    uint8_t previous_pressed = 0;

    ESP_LOGI(TAG, "Waveshare ESP32-S3-AUDIO-Board button RGB test started");
    ESP_LOGI(TAG, "WiFi, OpenAI, audio, ES7210 and ES8311 are not used in this test.");
    ESP_LOGI(TAG, "K1 -> red, K2 -> green, K3 -> blue.");

    init_rgb_leds();
    init_tca9555_buttons();

    while (true) {
        uint8_t port_1 = 0;
        esp_err_t ret = tca9555_read_reg(TCA9555_INPUT_PORT_1_REG, &port_1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "TCA9555 read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint8_t pressed = (~port_1) & KEY_MASK;
        uint8_t newly_pressed = pressed & ~previous_pressed;

        if (newly_pressed & KEY_K1_BIT) {
            ESP_LOGI(TAG, "K1 pressed -> red");
            set_all_leds(32, 0, 0);
        }
        if (newly_pressed & KEY_K2_BIT) {
            ESP_LOGI(TAG, "K2 pressed -> green");
            set_all_leds(0, 32, 0);
        }
        if (newly_pressed & KEY_K3_BIT) {
            ESP_LOGI(TAG, "K3 pressed -> blue");
            set_all_leds(0, 0, 32);
        }

        previous_pressed = pressed;

        if ((heartbeat % 50) == 0) {
            ESP_LOGI(TAG, "Heartbeat: %lu", (unsigned long)(heartbeat / 50));
        }
        heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
