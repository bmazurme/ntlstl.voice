#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Runs the full voice pipeline for one recorded clip:
//   1. POST the WAV to <base>/api/transcribe   -> transcript
//   2. POST { text } to <base>/api/llm-command -> structured HA command
//   3. POST that command to <base>/api/ha-execute -> executed in Home Assistant
// <base> is CONFIG_P4_BACKEND_URL with its "/api/transcribe" suffix
// stripped. Logs progress at each stage; a failure at any stage just stops
// the pipeline there (nothing meaningful left to chain).
esp_err_t backend_client_run_pipeline(const int16_t *samples, size_t num_samples, uint32_t sample_rate);
