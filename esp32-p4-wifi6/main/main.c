#include "esp_log.h"
#include "nvs_flash.h"

#include "audio_pipeline.h"
#include "backend_client.h"
#include "wifi.h"

static const char *TAG = "main";

static void on_clip_ready(const int16_t *samples, size_t num_samples, uint32_t sample_rate)
{
    if (backend_client_run_pipeline(samples, num_samples, sample_rate) != ESP_OK) {
        ESP_LOGW(TAG, "Voice pipeline failed");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Connecting to WiFi (radio on the on-board ESP32-C6)...");
    wifi_connect_blocking();

    ESP_LOGI(TAG, "Starting audio pipeline (say the wake word to record)...");
    audio_pipeline_start(on_clip_ready);
}
