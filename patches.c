#include "patches.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Sampling_Mode_Bits {
    BITS16 = 0x01,
    UNSIGNED = 0x02,
    LOOPING = 0x04,
    PINGPONG = 0x08,
    REVERSE = 0x10,
    SUSTAIN = 0x20,
    ENVELOPE = 0x40,
    CLAMPED_RELEASE = 0x80
};

static uint8 read_u8 (FILE* f) {
    int c = fgetc(f);
    if (c == EOF) {
        printf("File too short.\n");
        exit(1);
    }
    return c;
}

static uint16 read_u16 (FILE* f) {
    uint16 r = read_u8(f);
    r |= read_u8(f) << 8;
    return r;
}

static uint32 read_u32 (FILE* f) {
    uint32 r = read_u8(f);
    r |= read_u8(f) << 8;
    r |= read_u8(f) << 16;
    r |= read_u8(f) << 24;
    return r;
}

static uint8* read_size (FILE* f, uint32 size) {
    uint8* data = malloc(size);
    int got = fread(data, 1, size, f);
    if (got != size) {
        free(data);
        printf("File too short.\n");
        exit(1);
    }
    return data;
}

static void skip (FILE* f, uint32 size) {
    for (uint32 i = 0; i < size; i++) {
        if (fgetc(f) == EOF) {
            printf("File too short.\n");
            exit(1);
        }
    }
}

static void require (FILE* f, uint32 size, const char* str) {
    for (const char* p = str; p < str+size; p++) {
        uint8 got = read_u8(f);
        if (got != (uint8)*p) {
            printf("File is incorrect: expected 0x%02hhX but got 0x%02hhX\n", (uint8)*p, got);
            exit(1);
        }
    }
}

Patch* load_patch (const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("Couldn't open %s for reading: %s\n", filename, strerror(errno));
        exit(1);
    }
    require(f, 9, "GF1PATCH1");
    skip(f, 1);
    require(f, 12, "0\x00ID#000002\x00");
    skip(f, 60);  // Description
    if (read_u8(f) > 1) {
        printf("Pat has too many instruments\n");
        exit(1);
    }
    skip(f, 1);  // Voices?
    skip(f, 1);  // Channels?
    skip(f, 2);  // Waveforms?
    Patch* pat = malloc(sizeof(Patch));
    pat->samples = NULL;
    pat->volume = read_u16(f);
    skip(f, 4);  // Data size
    skip(f, 36);  // Reserved
    if (read_u16(f) != 0) {
        printf("Instrument ID (?) was not 0x0000\n");
        goto fail;
    }
    skip(f, 16);  // Instrument name
    skip(f, 4);  // Instrument size
    if (read_u8(f) != 1) {
        printf("Instrument has too many layers (?)\n");
        goto fail;
    }
    skip(f, 40);  // Reserved
    if (read_u8(f) != 0) {
        printf("Layer duplicate (?) not 0 (?)\n");
        goto fail;
    }
    if (read_u8(f) != 0) {
        printf("Layer ID (?) not 0 (?)\n");
        goto fail;
    }
    skip(f, 4);  // Layer size
    pat->n_samples = read_u8(f);
    skip(f, 40);  // Reserved
    pat->samples = malloc(pat->n_samples * sizeof(Sample));
    for (uint8 i = 0; i < pat->n_samples; i++) {
        pat->samples[i].data = NULL;
    }
    for (uint8 i = 0; i < pat->n_samples; i++) {
        skip(f, 7);  // Wave name
        if (read_u8(f) != 0) {
            printf("Fractions byte (?) not 0\n");
            goto fail;
        }
        pat->samples[i].data_size = read_u32(f);
        pat->samples[i].loop_start = read_u32(f);
        pat->samples[i].loop_end = read_u32(f);
        pat->samples[i].sample_rate = read_u16(f);
        pat->samples[i].low_freq = read_u32(f);
        pat->samples[i].high_freq = read_u32(f);
        pat->samples[i].root_freq = read_u32(f);
        skip(f, 2);  // Tune
        pat->samples[i].pan = read_u8(f);
        for (uint32 i = 0; i < 6; i++) {
            pat->samples[i].envelope_rates[i] = read_u8(f);
        }
        for (uint32 i = 0; i < 6; i++) {
            pat->samples[i].envelope_offsets[i] = read_u8(f);
        }
        skip(f, 6);  // Tremolo and vibrato stuff
        uint8 sampling_modes = read_u8(f);
        skip(f, 4);  // Scale(?) stuff
        skip(f, 36);  // Reserved
        pat->samples[i].data = read_size(f, pat->samples[i].data_size);
        if (!(sampling_modes & BITS16)) {
            printf("8-bit samples NYI\n");
            goto fail;
        }
        if (sampling_modes & UNSIGNED) {
            for (uint32 j = 0; j < pat->samples[i].data_size; j += 2) {
                *(uint16*)(pat->samples[i].data + j) ^= 0x8000;
            }
        }
        pat->samples[i].loop = !!(sampling_modes & LOOPING);
        if (sampling_modes & PINGPONG) {
            printf("ping-pong samples NYI\n");
            goto fail;
        }
        if (sampling_modes & REVERSE) {
            printf("reverse samples NYI\n");
            goto fail;
        }
    }
    fclose(f);
    return pat;

  fail:
    fclose(f);
    free_patch(pat);
    exit(1);
}

void free_patch (Patch* pat) {
    if (pat->samples) {
        for (uint32 i = 0; i < pat->n_samples; i++) {
            if (pat->samples[i].data)
                free(pat->samples[i].data);
        }
        free(pat->samples);
    }
    free(pat);
}

void print_patch (Patch* pat) {
    printf("Patch: {\n");
    printf("  volume: %hu\n", pat->volume);
    for (uint8 i = 0; i < pat->n_samples; i++) {
        printf("  Sample: {\n");
        printf("    low_freq: %u\n", pat->samples[i].low_freq);
        printf("    high_freq: %u\n", pat->samples[i].high_freq);
        printf("    root_freq: %u\n", pat->samples[i].root_freq);
        printf("    loop_start: %u\n", pat->samples[i].loop_start);
        printf("    loop_end: %u\n", pat->samples[i].loop_end);
        printf("    envelope_rates: %hhu %hhu %hhu %hhu %hhu %hhu\n",
            pat->samples[i].envelope_rates[0], pat->samples[i].envelope_rates[1],
            pat->samples[i].envelope_rates[2], pat->samples[i].envelope_rates[3],
            pat->samples[i].envelope_rates[4], pat->samples[i].envelope_rates[5]
        );
        printf("    envelope_offsets: %hhu %hhu %hhu %hhu %hhu %hhu\n",
            pat->samples[i].envelope_offsets[0], pat->samples[i].envelope_offsets[1],
            pat->samples[i].envelope_offsets[2], pat->samples[i].envelope_offsets[3],
            pat->samples[i].envelope_offsets[4], pat->samples[i].envelope_offsets[5]
        );
        printf("    sample_rate: %hu\n", pat->samples[i].sample_rate);
        printf("    data_size: %u\n", pat->samples[i].data_size);
        printf("    A bit of data: %hx %hx %hx %hx %hx %hx %hx %hx\n",
            pat->samples[i].data[0], pat->samples[i].data[1],
            pat->samples[i].data[2], pat->samples[i].data[3],
            pat->samples[i].data[4], pat->samples[i].data[5],
            pat->samples[i].data[6], pat->samples[i].data[7]
        );
        printf("  }\n");
    }
    printf("}\n");
}
