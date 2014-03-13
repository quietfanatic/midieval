#include "patches.h"

#include <ctype.h>
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
        printf("Instrument ID (?) was not 0x0000 in %s\n", filename);
        goto fail;
    }
    skip(f, 16);  // Instrument name
    skip(f, 4);  // Instrument size
    if (read_u8(f) != 1) {
        printf("Instrument has too many layers (?) in %s\n", filename);
        goto fail;
    }
    skip(f, 40);  // Reserved
    if (read_u8(f) != 0) {
        printf("Layer duplicate (?) not 0 (?) in %s\n", filename);
        goto fail;
    }
    if (read_u8(f) != 0) {
        printf("Layer ID (?) not 0 (?) in %s\n", filename);
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
            printf("Warning: NYI non-zero fractions byte (?) in %s\n", filename);
        }
        pat->samples[i].data_size = read_u32(f) / 2;
        pat->samples[i].loop_start = read_u32(f);
        pat->samples[i].loop_start /= 2;
        pat->samples[i].loop_end = read_u32(f);
        pat->samples[i].loop_end /= 2;
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
        pat->samples[i].data = (int16*)read_size(f, pat->samples[i].data_size * 2);
        if (!(sampling_modes & BITS16)) {
            printf("8-bit samples NYI\n");
            goto fail;
        }
        if (sampling_modes & UNSIGNED) {
            for (uint32 j = 0; j < pat->samples[i].data_size; j += 2) {
                pat->samples[i].data[j] ^= 0x8000;
            }
        }
        pat->samples[i].loop = !!(sampling_modes & LOOPING);
        pat->samples[i].pingpong = !!(sampling_modes & PINGPONG);
        if (sampling_modes & REVERSE) {
            printf("reverse samples NYI in %s\n", filename);
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

Bank* new_bank () {
    Bank* bank = malloc(sizeof(Bank));
    for (uint8 i = 0; i < 128; i++) {
        bank->patches[i] = NULL;
    }
    return bank;
}
void set_patch (Bank* bank, uint8 instrument, const char* filename) {
    if (instrument < 128) {
        bank->patches[instrument] = load_patch(filename);
    }
}
void free_bank (Bank* bank) {
    for (uint8 i = 0; i < 128; i++) {
        if (bank->patches[i])
            free_patch(bank->patches[i]);
    }
    free(bank);
}

char* read_word (char** p, char* end) {
    char* r = *p;
    while (*p != end && !isspace(**p) && **p != '=' && **p != '#') {
        (*p)++;
    }
    return r;
}
int32 read_i32 (char** p, char* end) {
    if (*p == end) {
        fprintf(stderr, "Parse error: expected number but got EOF\n");
        exit(1);
    }
    if (!isdigit(**p)) {
        fprintf(stderr, "Parse error: expected number but got '%c'\n", **p);
        exit(1);
    }
    int32 r = 0;
    while (*p != end && isdigit(**p)) {
        r *= 10;
        r += **p - '0';
        (*p)++;
    }
    return r;
}
void require_char (char** p, char* end, char c) {
    if (*p == end) {
        fprintf(stderr, "Parse error: expected '%c' but got EOF\n", c);
        exit(1);
    }
    if (**p != c) {
        fprintf(stderr, "Parse error: expected '%c' but got '%c'\n", c, **p);
        exit(1);
    }
    (*p)++;
}
void skip_ws (char** p, char* end) {
    while (*p != end && (**p == ' ' || **p == '\t'))
        (*p)++;
    if (*p != end && **p == '#') {
        while (*p != end && **p != '\n') {
            (*p)++;
        }
    }
}
int cmp_strs (char* a, size_t as, const char* b, size_t bs) {
    return as == bs && strncmp(a, b, as) == 0;
}

uint32 line;
char* line_begin;

void line_break (char** p, char* end) {
    require_char(p, end, '\n');
    line += 1;
    line_begin = *p;
}

Bank* load_bank (const char* cfg) {
    int32 prefix = -1;
    for (int32 i = 0; cfg[i]; i++) {
        if (cfg[i] == '/') prefix = i + 1;
    }
    FILE* f = fopen(cfg, "r");
    if (!f) {
        fprintf(stderr, "Could not open %s for reading: %s\n", cfg, strerror(errno));
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* dat = malloc(size);
    if (fread(dat, 1, size, f) != size) {
        fprintf(stderr, "Could not read from %s: %s\n", cfg, strerror(errno));
        exit(1);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "Could not close %s: %s\n", cfg, strerror(errno));
        exit(1);
    }
    char* p = dat;
    char* end = dat + size;
    line = 1;
    line_begin = dat;

    Bank* bank = new_bank();
    uint32 bank_num = 0;
    int drumset = 0;

    skip_ws(&p, end);
    while (p != end) {
        if (isalpha(*p)) {
            char* word = read_word(&p, end);
            if (cmp_strs(word, p - word, "bank", 4)) {
                skip_ws(&p, end);
                bank_num = read_i32(&p, end);
                drumset = 0;
            }
            else if (cmp_strs(word, p - word, "drumset", 7)) {
                skip_ws(&p, end);
                bank_num = read_i32(&p, end);
                drumset = 1;
            }
            else {
                fprintf(stderr, "Unrecognized command beginning with '%c'\n", *word);
                exit(1);
            }
            skip_ws(&p, end);
            line_break(&p, end);
        }
        else if (isdigit(*p)) {
            int32 program = read_i32(&p, end);
            if (program < 0 || program > 127) {
                fprintf(stderr, "Invalid program number: %d at %u:%lu (%lu)\n", program, line, p - line_begin, p - dat);
                exit(1);
            }
            skip_ws(&p, end);
            char* word = read_word(&p, end);
            if (!drumset && bank_num == 0) {
                char* filename = malloc(prefix + (p - word) + 5);
                memcpy(filename, cfg, prefix);
                memcpy(filename + prefix, word, p - word);
                memcpy(filename + prefix + (p - word), ".pat", 5);
                if (bank->patches[program]) {
                    free_patch(bank->patches[program]);
                }
                bank->patches[program] = load_patch(filename);
                free(filename);
            }
            skip_ws(&p, end);
            while (p != end && *p != '\n') {
                 // For now, just skip all the parameters
                char* word = read_word(&p, end);
                skip_ws(&p, end);
                require_char(&p, end, '=');
                skip_ws(&p, end);
                char* word2 = read_word(&p, end);
                skip_ws(&p, end);
            }
            line_break(&p, end);
        }
        else if (*p == '\n') {
            line_break(&p, end);
        }
        else {
            fprintf(stderr, "Parse error: Unexpected char \\x%02hhX at %u:%lu\n", *p, line, p - line_begin);
            exit(1);
        }
        skip_ws(&p, end);
    }
    free(dat);
    return bank;
}
