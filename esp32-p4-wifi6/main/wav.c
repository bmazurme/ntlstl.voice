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

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool wav_parse(const uint8_t *buf, size_t len, const int16_t **out_pcm,
               size_t *out_num_samples, uint32_t *out_sample_rate)
{
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint32_t sample_rate = 0;
    uint16_t channels = 0, bits_per_sample = 0;
    bool have_fmt = false;
    size_t pos = 12;

    // Walk RIFF sub-chunks looking for "fmt " (need it first, to validate
    // the format) and "data" (the actual PCM payload). Chunks are
    // word-aligned, so odd-sized chunks get a trailing pad byte.
    while (pos + 8 <= len) {
        uint32_t chunk_size = get_le32(buf + pos + 4);
        size_t data_pos = pos + 8;

        if (memcmp(buf + pos, "fmt ", 4) == 0 && data_pos + 16 <= len) {
            channels = get_le16(buf + data_pos + 2);
            sample_rate = get_le32(buf + data_pos + 4);
            bits_per_sample = get_le16(buf + data_pos + 14);
            have_fmt = true;
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            if (!have_fmt || channels != 1 || bits_per_sample != 16 || data_pos > len) {
                return false;
            }
            size_t avail = len - data_pos;
            size_t data_len = chunk_size < avail ? chunk_size : avail;
            *out_pcm = (const int16_t *)(buf + data_pos);
            *out_num_samples = data_len / sizeof(int16_t);
            *out_sample_rate = sample_rate;
            return true;
        }

        pos = data_pos + chunk_size + (chunk_size & 1);
    }
    return false;
}
