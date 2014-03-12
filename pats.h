#ifndef HAVE_MIDIVAL_PATS_H
#define HAVE_MIDIVAL_PATS_H

#include <inttypes.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef struct Sample {
    uint32 low_freq;
    uint32 high_freq;
    uint32 root_freq;
    uint32 loop_start;
    uint32 loop_end;
    uint8 envelope_rates [6];
    uint8 envelope_offsets [6];
    uint8 pan;
    uint16 sample_rate;
    uint32 data_size;
    uint8* data;
} Sample;

typedef struct Pat {
     // TODO: do we need any more information?
    uint16 volume;
    uint8 n_samples;
    Sample* samples;
} Pat;

Pat* load_pat (const char* filename);
void free_pat (Pat*);

void print_pat (Pat*);

#endif
