idf_component_register(
    SRCS "main.c" "wav.c" "backend_client.c" "wifi.c" "audio_pipeline.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_client driver esp_codec_dev esp_wifi_remote esp-sr json
)
