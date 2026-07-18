#include "audio_pipeline.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"

#define SAMPLE_RATE 16000
#define RECORD_SAMPLES (SAMPLE_RATE * CONFIG_P4_RECORD_SECONDS)

static const char *TAG = "audio_pipeline";

typedef enum {
    STATE_LISTENING,
    STATE_RECORDING,
    STATE_UPLOADING,
    STATE_PLAYING,
} app_state_t;

static esp_codec_dev_handle_t s_mic;
static const esp_afe_sr_iface_t *s_afe_handle;
static esp_afe_sr_data_t *s_afe_data;
static audio_clip_ready_cb_t s_on_clip_ready;
static esp_codec_dev_sample_info_t s_listen_fs;
static bool s_playback_enabled;

static volatile app_state_t s_state = STATE_LISTENING;
static int16_t *s_record_buf;
static size_t s_record_pos;

// I2S master channel(s) that provide MCLK/BCLK/WS to the ES8311 and carry
// its ADC output (mic, DIN) plus, if P4_I2S_DOUT_GPIO is configured, its
// DAC input (speaker, DOUT). The P4 is always the I2S master, the ES8311
// the slave. Requesting both tx and rx from the same i2s_new_channel call
// makes them a synchronized full-duplex pair sharing one clock generator;
// *out_tx is left NULL when playback isn't configured (out_rx-only, same
// as before speaker support existed).
static void i2s_init(i2s_chan_handle_t *out_tx, i2s_chan_handle_t *out_rx)
{
    bool want_tx = CONFIG_P4_I2S_DOUT_GPIO >= 0;

    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, want_tx ? &tx : NULL, &rx));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = CONFIG_P4_I2S_MCLK_GPIO,
            .bclk = CONFIG_P4_I2S_BCLK_GPIO,
            .ws = CONFIG_P4_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_P4_I2S_DIN_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    // ES8311 expects MCLK = 256 * sample_rate.
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std_cfg));

    if (want_tx) {
        std_cfg.gpio_cfg.dout = CONFIG_P4_I2S_DOUT_GPIO;
        std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    }

    *out_tx = tx;
    *out_rx = rx;
}

// Configures the ES8311 over I2C (via esp_codec_dev) so its microphone ADC
// streams onto the I2S bus set up above, and - if P4_I2S_DOUT_GPIO is
// configured - its DAC can drive the onboard speaker for TTS playback.
static void codec_init(void)
{
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_P4_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_P4_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_init(&tx, &rx);

    audio_codec_i2s_cfg_t i2s_if_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_if_cfg);

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    s_playback_enabled = (CONFIG_P4_I2S_DOUT_GPIO >= 0);

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = s_playback_enabled ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_ADC,
        .master_mode = false,                      // ES8311 is the I2S slave
        .use_mclk = true,
        .digital_mic = false,                      // analog mic on MIC1
        .pa_pin = CONFIG_P4_PA_ENABLE_GPIO,         // -1 unless the board needs a dedicated PA enable pin
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = s_playback_enabled ? ESP_CODEC_DEV_TYPE_IN_OUT : ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_mic = esp_codec_dev_new(&dev_cfg);

    s_listen_fs.sample_rate = SAMPLE_RATE;
    s_listen_fs.channel = 1;
    s_listen_fs.bits_per_sample = 16;
    ESP_ERROR_CHECK(esp_codec_dev_open(s_mic, &s_listen_fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(s_mic, (float)CONFIG_P4_MIC_GAIN_DB));

    ESP_LOGI(TAG, "ES8311 ready (%d Hz mic, gain %d dB, playback %s)", SAMPLE_RATE, CONFIG_P4_MIC_GAIN_DB,
             s_playback_enabled ? "enabled" : "disabled (set P4_I2S_DOUT_GPIO)");
}

// Reads 16-bit mono samples from the ES8311 and feeds them into the AFE
// pipeline at whatever chunk size it expects.
static void feed_task(void *arg)
{
    int chunk_samples = s_afe_handle->get_feed_chunksize(s_afe_data);
    int feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);

    int16_t *feed_buf = malloc(chunk_samples * feed_channels * sizeof(int16_t));
    assert(feed_buf);

    while (true) {
        if (s_state == STATE_PLAYING) {
            // The codec is closed/reopened for output for the duration of
            // audio_pipeline_play(); reading from it here would race with
            // that reconfiguration.
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int ret = esp_codec_dev_read(s_mic, feed_buf, chunk_samples * feed_channels * sizeof(int16_t));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "codec read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        s_afe_handle->feed(s_afe_data, feed_buf);
    }
}

// Runs the (slow, blocking) HTTP upload off the fetch task, so fetch_task
// never stalls draining the AFE ring buffer. Without this, feed_task keeps
// pushing samples in while fetch_task is stuck in the HTTP POST, and the
// AFE ring buffer overflows ("Ringbuffer of AFE(FEED) is full") for the
// whole duration of the upload (backend transcription can take 10s+ on
// modest hardware).
static void upload_task(void *arg)
{
    ESP_LOGI(TAG, "Recording complete, uploading...");
    s_on_clip_ready(s_record_buf, RECORD_SAMPLES, SAMPLE_RATE);
    // The upload can take tens of seconds (slow backend/whisper), during
    // which feed_task keeps pushing live mic audio through AFE (to avoid
    // the ring-buffer-full warning) even though nobody acts on it. Reset
    // the AFE ring buffer before going back to STATE_LISTENING so WakeNet
    // starts clean instead of catching up on a backlog of stale audio.
    s_afe_handle->reset_buffer(s_afe_data);
    ESP_LOGI(TAG, "Upload done, listening again");
    s_state = STATE_LISTENING;
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    while (true) {
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            continue;
        }

        if (s_state == STATE_LISTENING && res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "Wake word detected, recording %d s...", CONFIG_P4_RECORD_SECONDS);
            s_record_pos = 0;
            s_state = STATE_RECORDING;
        }

        if (s_state == STATE_RECORDING && res->data) {
            size_t n = res->data_size / sizeof(int16_t);
            size_t remaining = RECORD_SAMPLES - s_record_pos;
            size_t to_copy = n < remaining ? n : remaining;
            memcpy(s_record_buf + s_record_pos, res->data, to_copy * sizeof(int16_t));
            s_record_pos += to_copy;

            if (s_record_pos >= RECORD_SAMPLES) {
                s_state = STATE_UPLOADING;
                xTaskCreatePinnedToCore(upload_task, "upload", 8 * 1024, NULL, 5, NULL, 0);
            }
        }
    }
}

void audio_pipeline_start(audio_clip_ready_cb_t on_clip_ready)
{
    s_on_clip_ready = on_clip_ready;

    srmodel_list_t *models = esp_srmodel_init("model");
    char *wakenet_model = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (!wakenet_model) {
        ESP_LOGE(TAG, "No wake word model found in the 'model' partition - flash it first");
        return;
    }
    ESP_LOGI(TAG, "Using wake word model: %s", wakenet_model);

    // esp-sr v2 API (v2 is the first release with ESP32-P4 support).
    // "M" = a single microphone channel, no reference channel.
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_config->aec_init = false; // no speaker output, nothing to cancel
    afe_config->se_init = false;  // beamforming needs >=2 mics, we only have 1
    // wakenet_mode is the detection *threshold*: DET_MODE_95 fires only at
    // >=0.95 confidence (strict — needs a loud/clean signal, so live speech
    // was missed and only phone-TTS playback triggered reliably), while
    // DET_MODE_90 fires at >=0.90 (more sensitive, catches normal-volume
    // live voice). The extra false-alarm risk is fine here since waking only
    // starts a recording, not an irreversible action.
    afe_config->wakenet_mode = DET_MODE_90;

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    s_record_buf = heap_caps_malloc(RECORD_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    assert(s_record_buf && "Not enough PSRAM for recording buffer");

    codec_init();

    xTaskCreatePinnedToCore(feed_task, "afe_feed", 8 * 1024, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(fetch_task, "afe_fetch", 8 * 1024, NULL, 5, NULL, 1);
}

void audio_pipeline_play(const int16_t *pcm, size_t num_samples, uint32_t sample_rate)
{
    if (!s_playback_enabled) {
        ESP_LOGW(TAG, "TTS playback requested but P4_I2S_DOUT_GPIO is not configured, skipping");
        return;
    }
    if (!pcm || num_samples == 0) {
        return;
    }

    s_state = STATE_PLAYING;
    esp_codec_dev_close(s_mic);

    esp_codec_dev_sample_info_t out_fs = {
        .sample_rate = sample_rate,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_mic, &out_fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to open codec for playback");
    } else {
        esp_codec_dev_set_out_vol(s_mic, CONFIG_P4_SPEAKER_VOLUME);
        // esp_codec_dev_write() wants a non-const pointer (a software
        // volume path may adjust levels in place); we only ever pass
        // buffers we own here, so discarding const is safe.
        int ret = esp_codec_dev_write(s_mic, (void *)pcm, (int)(num_samples * sizeof(int16_t)));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "codec write failed: %d", ret);
        }
        esp_codec_dev_close(s_mic);
    }

    ESP_ERROR_CHECK(esp_codec_dev_open(s_mic, &s_listen_fs));
    s_afe_handle->reset_buffer(s_afe_data);
    s_state = STATE_LISTENING;
}
