#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ESP32_S3_DEMO";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 AUDIO BOARD DEMO STARTED");
    ESP_LOGI(TAG, "Это безопасный тест: WiFi, OpenAI и audio сейчас не используются.");

    int counter = 0;

    while (1) {
        ESP_LOGI(TAG, "Heartbeat: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
