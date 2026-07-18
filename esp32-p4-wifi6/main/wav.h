#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WAV_HEADER_SIZE 44

// Fills a 44-byte canonical PCM WAV header (mono/stereo, 16-bit assumed by
// the rest of this project, but bits_per_sample is kept as a parameter for
// clarity).
void wav_write_header(uint8_t header[WAV_HEADER_SIZE], uint32_t pcm_bytes,
                       uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);

// Parses a mono 16-bit PCM WAV file (as produced by the backend's
// /api/tts, i.e. Piper's output) and points *out_pcm at the "data" chunk
// in-place (no copy - buf must outlive the returned pointer). Returns
// false if buf isn't RIFF/WAVE, has no "fmt "/"data" chunk, or isn't
// mono 16-bit.
bool wav_parse(const uint8_t *buf, size_t len, const int16_t **out_pcm,
               size_t *out_num_samples, uint32_t *out_sample_rate);
