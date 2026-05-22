idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        log
        freertos
        esp_driver_i2c
        led_strip
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
)
