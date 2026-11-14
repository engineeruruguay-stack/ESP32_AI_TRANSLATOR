idf_component_register(
    SRCS "main.c" "board_audio.c" "music.c" "stream.c"
    INCLUDE_DIRS "."
    REQUIRES
        log
        freertos
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_i2s
        esp_driver_sdmmc
        fatfs
        heap
        lwip
        led_strip
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
        app_update
)
