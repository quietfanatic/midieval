#include "midieval.h"

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

static uint8_t read_u8 (FILE* f) {
    int c = fgetc(f);
    if (c == EOF) {
        printf("File too short.\n");
        exit(1);
    }
    return c;
}

static uint16_t read_u16 (FILE* f) {
    uint16_t r = read_u8(f);
    r |= read_u8(f) << 8;
    return r;
}

static uint32_t read_u32 (FILE* f) {
    uint32_t r = read_u8(f);
    r |= read_u8(f) << 8;
    r |= read_u8(f) << 16;
    r |= read_u8(f) << 24;
    return r;
}

static uint8_t* read_size (FILE* f, uint32_t size) {
    uint8_t* data = malloc(size);
    int got = fread(data, 1, size, f);
    if (got != size) {
        free(data);
        printf("File too short.\n");
        exit(1);
    }
    return data;
}

static void skip (FILE* f, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (fgetc(f) == EOF) {
            printf("File too short.\n");
            exit(1);
        }
    }
}

static void require (FILE* f, uint32_t size, const char* str) {
    for (const char* p = str; p < str+size; p++) {
        uint8_t got = read_u8(f);
        if (got != (uint8_t)*p) {
            printf("File is incorrect: expected 0x%02hhX but got 0x%02hhX\n", (uint8_t)*p, got);
            exit(1);
        }
    }
}

MDV_Patch* _mdv_load_patch (const char* filename) {
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
    MDV_Patch* pat = malloc(sizeof(MDV_Patch));
    pat->samples = NULL;
    pat->note = -1;
    pat->keep_envelope = 0;
    pat->keep_loop = 0;
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
    pat->samples = malloc(pat->n_samples * sizeof(MDV_Sample));
    for (uint8_t i = 0; i < pat->n_samples; i++) {
        pat->samples[i].data = NULL;
    }
    for (uint8_t i = 0; i < pat->n_samples; i++) {
        skip(f, 7);  // Wave name
        uint8_t fractions = read_u8(f);
        pat->samples[i].data_size = read_u32(f) / 2;
        pat->samples[i].loop_start = read_u32(f) * 0x100000000LL
                                   + (fractions & 0xf) * 0x010000000LL;
        pat->samples[i].loop_start /= 2;
        pat->samples[i].loop_end = read_u32(f) * 0x100000000LL
                                   + ((fractions >> 4) & 0xf) * 0x010000000LL;
        pat->samples[i].loop_end /= 2;
        pat->samples[i].sample_inc = read_u16(f) * 0x100000000LL / MDV_SAMPLE_RATE;
        pat->samples[i].low_freq = read_u32(f) * 0x10000LL / 1000;
        pat->samples[i].high_freq = read_u32(f) * 0x10000LL / 1000;
        pat->samples[i].root_freq = read_u32(f) * 0x10000LL / 1000;
        skip(f, 2);  // Tune
        pat->samples[i].pan = read_u8(f);
         // These formulas are pretty much stolen from TiMidity,
         //  which uses 15:15 (?) fixed-point format, so we'll just
         //  go ahead and copy that for now.
        for (uint32_t j = 0; j < 6; j++) {
            uint8_t byte = read_u8(f);
            uint32_t val = (uint32_t)(byte & 0x3f) << (3 * (3 - ((byte >> 6) & 3)));
            pat->samples[i].envelope_rates[j] = (val * 44100 / MDV_SAMPLE_RATE) << 9;
        }
        for (uint32_t j = 0; j < 6; j++) {
            pat->samples[i].envelope_offsets[j] = read_u8(f) << 22;
        }
         // Tremolo and vibrato.
         // These 38s are an arbitrary scaling factor copied from Timidity
         // Increasing them makes tremolo and vibrato go slower
        uint32_t trs = read_u8(f);
        pat->samples[i].tremolo_sweep_increment = !trs ? 0 :
            (38 * 0x1000000) / (MDV_SAMPLE_RATE * trs);
        uint32_t trp = read_u8(f);
        pat->samples[i].tremolo_phase_increment =
            (trp * 0x1000000) / (38 * MDV_SAMPLE_RATE);
        pat->samples[i].tremolo_depth = read_u8(f);
        uint32_t vbs = read_u8(f);
        pat->samples[i].vibrato_sweep_increment = !vbs ? 0 :
            (38 * 0x1000000) / (MDV_SAMPLE_RATE * vbs);
        uint32_t vbr = read_u8(f);
        pat->samples[i].vibrato_phase_increment =
            (vbr * 0x1000000) / (38 * MDV_SAMPLE_RATE);
        pat->samples[i].vibrato_depth = read_u8(f);

        uint8_t sampling_modes = read_u8(f);
        skip(f, 4);  // Scale(?) stuff
        skip(f, 36);  // Reserved
        pat->samples[i].data = (int16_t*)read_size(f, pat->samples[i].data_size * 2);
        if (!(sampling_modes & BITS16)) {
            printf("8-bit samples NYI\n");
            goto fail;
        }
        if (sampling_modes & UNSIGNED) {
            for (uint32_t j = 0; j < pat->samples[i].data_size; j++) {
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
    mdv_free_patch(pat);
    exit(1);
}

void mdv_free_patch (MDV_Patch* pat) {
    if (pat->samples) {
        for (uint32_t i = 0; i < pat->n_samples; i++) {
            if (pat->samples[i].data)
                free(pat->samples[i].data);
        }
        free(pat->samples);
    }
    free(pat);
}

void mdv_print_patch (MDV_Patch* pat) {
    printf("Patch: {\n");
    printf("  volume: %hu\n", pat->volume);
    for (uint8_t i = 0; i < pat->n_samples; i++) {
        printf("  Sample: {\n");
        printf("    low_freq: %u\n", pat->samples[i].low_freq);
        printf("    high_freq: %u\n", pat->samples[i].high_freq);
        printf("    root_freq: %u\n", pat->samples[i].root_freq);
        printf("    loop_start: %llu\n", (long long unsigned)pat->samples[i].loop_start);
        printf("    loop_end: %llu\n", (long long unsigned)pat->samples[i].loop_end);
        printf("    envelope_rates: %u %u %u %u %u %u\n",
            pat->samples[i].envelope_rates[0], pat->samples[i].envelope_rates[1],
            pat->samples[i].envelope_rates[2], pat->samples[i].envelope_rates[3],
            pat->samples[i].envelope_rates[4], pat->samples[i].envelope_rates[5]
        );
        printf("    envelope_offsets: %u %u %u %u %u %u\n",
            pat->samples[i].envelope_offsets[0], pat->samples[i].envelope_offsets[1],
            pat->samples[i].envelope_offsets[2], pat->samples[i].envelope_offsets[3],
            pat->samples[i].envelope_offsets[4], pat->samples[i].envelope_offsets[5]
        );
        printf("    pan: %hhu\n", pat->samples[i].pan);
        printf("    loop: %hhu\n", pat->samples[i].loop);
        printf("    pingpong: %hhu\n", pat->samples[i].pingpong);
        printf("    sample_inc: %llu\n", (long long unsigned)pat->samples[i].sample_inc);
        printf("    data_size: %u\n", pat->samples[i].data_size);
        printf("    A bit of data: %04hx %04hx %04hx %04hx %04hx %04hx %04hx %04hx\n",
            pat->samples[i].data[0], pat->samples[i].data[1],
            pat->samples[i].data[2], pat->samples[i].data[3],
            pat->samples[i].data[4], pat->samples[i].data[5],
            pat->samples[i].data[6], pat->samples[i].data[7]
        );
        printf("  }\n");
    }
    printf("}\n");
}

void mdv_bank_init (MDV_Bank* bank) {
    for (uint8_t i = 0; i < 128; i++) {
        bank->patches[i] = NULL;
        bank->drums[i] = NULL;
    }
}
void mdv_bank_load_patch (MDV_Bank* bank, uint8_t instrument, const char* filename) {
    if (instrument < 128) {
        bank->patches[instrument] = _mdv_load_patch(filename);
    }
}
void mdv_bank_load_drum (MDV_Bank* bank, uint8_t instrument, const char* filename) {
    if (instrument < 128) {
        bank->drums[instrument] = _mdv_load_patch(filename);
    }
}
void mdv_bank_free_patches (MDV_Bank* bank) {
    for (uint8_t i = 0; i < 128; i++) {
        if (bank->patches[i]) {
            mdv_free_patch(bank->patches[i]);
            bank->patches[i] = NULL;
        }
        if (bank->drums[i]) {
            mdv_free_patch(bank->drums[i]);
            bank->drums[i] = NULL;
        }
    }
}

static char* read_word (char** p, char* end) {
    char* r = *p;
    while (*p != end && !isspace(**p) && **p != '=' && **p != '#') {
        (*p)++;
    }
    return r;
}
static int32_t read_i32 (char** p, char* end) {
    if (*p == end) {
        fprintf(stderr, "Parse error: expected number but got EOF\n");
        exit(1);
    }
    if (!isdigit(**p)) {
        fprintf(stderr, "Parse error: expected number but got '%c'\n", **p);
        exit(1);
    }
    int32_t r = 0;
    while (*p != end && isdigit(**p)) {
        r *= 10;
        r += **p - '0';
        (*p)++;
    }
    return r;
}
static void require_char (char** p, char* end, char c) {
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
static void skip_ws (char** p, char* end) {
    while (*p != end && (**p == ' ' || **p == '\t'))
        (*p)++;
    if (*p != end && **p == '#') {
        while (*p != end && **p != '\n') {
            (*p)++;
        }
    }
}
static int cmp_strs (char* a, size_t as, const char* b, size_t bs) {
    return as == bs && strncmp(a, b, as) == 0;
}

static uint32_t line;
static char* line_begin;

static void line_break (char** p, char* end) {
    require_char(p, end, '\n');
    line += 1;
    line_begin = *p;
}

void mdv_bank_load_config (MDV_Bank* bank, const char* cfg) {
    int32_t prefix = -1;
    for (int32_t i = 0; cfg[i]; i++) {
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

    uint32_t bank_num = 0;
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
            int32_t program = read_i32(&p, end);
            if (program < 0 || program > 127) {
                fprintf(stderr, "Invalid program number: %d at %u:%lu (%lu)\n", program, line, p - line_begin, p - dat);
                exit(1);
            }
            skip_ws(&p, end);
            char* word = read_word(&p, end);
            if (bank_num == 0) {
                char* filename = malloc(prefix + (p - word) + 5);
                memcpy(filename, cfg, prefix);
                memcpy(filename + prefix, word, p - word);
                memcpy(filename + prefix + (p - word), ".pat", 5);
                MDV_Patch* patch = _mdv_load_patch(filename);
                if (drumset) {
                    if (bank->drums[program]) {
                        mdv_free_patch(bank->drums[program]);
                    }
                    bank->drums[program] = patch;
                }
                else {
                    if (bank->patches[program]) {
                        mdv_free_patch(bank->patches[program]);
                    }
                    bank->patches[program] = patch;
                }
                free(filename);
                skip_ws(&p, end);
                while (p != end && *p != '\n') {
                    char* option = read_word(&p, end);
                    uint32_t opt_len = p - option;
                    skip_ws(&p, end);
                    require_char(&p, end, '=');
                    skip_ws(&p, end);
                    if (cmp_strs(option, opt_len, "amp", 3)) {
                        int32_t percent = read_i32(&p, end);
                        patch->volume = patch->volume * percent / 100;
                    }
                    else if (cmp_strs(option, opt_len, "note", 4)) {
                        int32_t note = read_i32(&p, end);
                        if (note >= 0 && note <= 127) {
                            patch->note = note;
                        }
                    }
                    else if (cmp_strs(option, opt_len, "keep", 4)) {
                        char* keep = read_word(&p, end);
                        if (cmp_strs(keep, p - keep, "loop", 4)) {
                            patch->keep_loop = 1;
                        }
                        else if (cmp_strs(keep, p - keep, "env", 3)) {
                            patch->keep_envelope = 1;
                        }
                    }
                    else {
                        read_word(&p, end);
                    }
                    skip_ws(&p, end);
                }
            }
            else {
                skip_ws(&p, end);
                while (p != end && *p != '\n') {
                     // Just skip all the parameters
                    read_word(&p, end);
                    skip_ws(&p, end);
                    require_char(&p, end, '=');
                    skip_ws(&p, end);
                    read_word(&p, end);
                    skip_ws(&p, end);
                }
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
}
