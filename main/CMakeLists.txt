idf_component_register(
    SRCS "main.c" "audio.c" "translator.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer log
)
