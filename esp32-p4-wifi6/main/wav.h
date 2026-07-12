#pragma once

#include <stddef.h>
#include <stdint.h>

#define WAV_HEADER_SIZE 44

// Fills a 44-byte canonical PCM WAV header (mono/stereo, 16-bit assumed by
// the rest of this project, but bits_per_sample is kept as a parameter for
// clarity).
void wav_write_header(uint8_t header[WAV_HEADER_SIZE], uint32_t pcm_bytes,
                       uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
