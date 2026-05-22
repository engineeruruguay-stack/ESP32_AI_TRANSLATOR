# ESP32 AI Translator — Current State

Board: Waveshare ESP32-S3-AUDIO-Board
Chip: ESP32-S3R8
Flash: 16MB
Working path: ~/Code/ESP32_AI_TRANSLATOR
Current USB port example: /dev/cu.usbmodem1101

## Working features

- ESP-IDF build / flash / monitor
- WiFi connection to iPhone hotspot
- RGB WS2812B LEDs on GPIO38
- Correct color order: GRB via set_pixel(green, red, blue)
- K1/K2/K3 buttons through TCA9555
- I2C: SDA GPIO11, SCL GPIO10, address 0x20
- Web panel on http://172.20.10.10
- OTA partitions prepared
- /ota page stub works

## Not implemented yet

- Real OTA binary upload
- Audio codec ES7210 / ES8311
- Microphone capture
- Speaker playback
- OpenAI API
- Voice translation
