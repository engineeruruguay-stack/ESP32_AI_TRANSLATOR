#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "led_strip.h"

#define LED_STRIP_GPIO 38
#define LED_COUNT 7

static const char *TAG = "LED_TEST";

static led_strip_handle_t led_strip;

static void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 AUDIO BOARD LED TEST STARTED");

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &led_strip
        )
    );

    led_strip_clear(led_strip);

    int counter = 0;

    while (1) {
        ESP_LOGI(TAG, "RED");
        set_all_leds(20, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "GREEN");
        set_all_leds(0, 20, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "BLUE");
        set_all_leds(0, 0, 20);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "OFF / Heartbeat: %d", counter++);
        led_strip_clear(led_strip);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
