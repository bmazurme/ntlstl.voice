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

// Plays a mono 16-bit PCM clip (e.g. the TTS response) through the onboard
// speaker. Blocks until playback finishes. Pauses mic capture for the
// duration (record and playback share the same I2S peripheral/codec and
// never run concurrently in this app) and resets the AFE ring buffer
// afterwards so WakeNet doesn't wake up to a backlog of stale audio.
// No-op if P4_I2S_DOUT_GPIO wasn't configured (defaults to disabled).
void audio_pipeline_play(const int16_t *pcm, size_t num_samples, uint32_t sample_rate);
