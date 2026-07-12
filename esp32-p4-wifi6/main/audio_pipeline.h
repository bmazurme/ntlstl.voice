#pragma once

#include <stddef.h>
#include <stdint.h>

// Called from the audio pipeline task once a fixed-duration clip has been
// recorded after a wake word trigger. Runs on the fetch task's stack, so
// keep it reasonably quick (it currently performs a blocking HTTP POST).
typedef void (*audio_clip_ready_cb_t)(const int16_t *samples, size_t num_samples, uint32_t sample_rate);

// Brings up the on-board ES8311 codec (I2C control + I2S master data path),
// the WakeNet/AFE pipeline (esp-sr v2), and starts the feed/fetch tasks.
// Call once from app_main() after WiFi is up.
void audio_pipeline_start(audio_clip_ready_cb_t on_clip_ready);
