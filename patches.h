#ifndef HAVE_MIDIVAL_PATCHES_H
#define HAVE_MIDIVAL_PATCHES_H

#include <inttypes.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int16_t int16;

typedef struct Sample {
     // in milliHz
    uint32 low_freq;
    uint32 high_freq;
    uint32 root_freq;
     // in samples
    uint32 loop_start;
    uint32 loop_end;
    uint8 envelope_rates [6];
    uint8 envelope_offsets [6];
    uint8 pan;
    uint8 loop;
    uint16 sample_rate;
    uint32 data_size;
    int16* data;
} Sample;

typedef struct Patch {
     // TODO: do we need any more information?
    uint16 volume;
    uint8 n_samples;
    Sample* samples;
} Patch;

Patch* load_patch (const char* filename);
void free_patch (Patch*);

void print_patch (Patch*);

typedef struct Bank {
    Patch* patches [128];
} Bank;

Bank* new_bank ();
void set_patch (Bank*, uint8 instrument, const char* filename);
void free_bank (Bank*);

#endif
