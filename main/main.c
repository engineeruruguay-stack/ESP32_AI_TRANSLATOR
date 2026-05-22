#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio.h"
#include "translator.h"

static const char *TAG = "ESP32_AI_TRANSLATOR";

void app_main(void)
{
    ESP_LOGI(TAG, "Инициализация голосового переводчика...");

    if (!audio_init()) {
        ESP_LOGE(TAG, "Ошибка инициализации аудио");
        return;
    }

    if (!translator_init()) {
        ESP_LOGE(TAG, "Ошибка инициализации переводчика");
        return;
    }

    while (true) {
        audio_frame_t frame = {0};
        if (audio_capture(&frame)) {
            const char *spanish_text = translator_recognize_speech(&frame);
            if (spanish_text != NULL) {
                ESP_LOGI(TAG, "Распознано: %s", spanish_text);
                const char *russian_text = translator_translate(spanish_text);
                if (russian_text != NULL) {
                    ESP_LOGI(TAG, "Перевод: %s", russian_text);
                    // TODO: добавить синтез речи или вывод на экран
                }
            }
            audio_frame_release(&frame);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
