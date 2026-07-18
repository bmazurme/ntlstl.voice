#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Runs the full voice pipeline for one recorded clip:
//   1. POST the WAV to <base>/api/transcribe   -> transcript
//   2. POST { text } to <base>/api/llm-command -> structured HA command
//      (also carries response_text, the reply to speak back)
//   3. POST that command to <base>/api/ha-execute -> executed in Home Assistant
//   4. POST { text: response_text } to <base>/api/tts -> WAV audio, played
//      back through the onboard speaker (audio_pipeline_play; a no-op if
//      P4_I2S_DOUT_GPIO isn't configured)
// <base> is CONFIG_P4_BACKEND_URL with its "/api/transcribe" suffix
// stripped. Logs progress at each stage; a failure at any stage just stops
// the pipeline there (nothing meaningful left to chain), except step 4
// which is best-effort (the HA command already executed by then).
esp_err_t backend_client_run_pipeline(const int16_t *samples, size_t num_samples, uint32_t sample_rate);
