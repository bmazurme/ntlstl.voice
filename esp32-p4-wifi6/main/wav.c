#include "wav.h"

#include <string.h>

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

void wav_write_header(uint8_t header[WAV_HEADER_SIZE], uint32_t pcm_bytes,
                       uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;

    memcpy(header + 0, "RIFF", 4);
    put_le32(header + 4, 36 + pcm_bytes);
    memcpy(header + 8, "WAVE", 4);

    memcpy(header + 12, "fmt ", 4);
    put_le32(header + 16, 16); // PCM fmt chunk size
    put_le16(header + 20, 1);  // audio format = PCM
    put_le16(header + 22, channels);
    put_le32(header + 24, sample_rate);
    put_le32(header + 28, byte_rate);
    put_le16(header + 32, block_align);
    put_le16(header + 34, bits_per_sample);

    memcpy(header + 36, "data", 4);
    put_le32(header + 40, pcm_bytes);
}
