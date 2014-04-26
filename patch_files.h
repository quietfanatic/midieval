#ifndef HAVE_MIDIVAL_PATCH_FILES_H
#define HAVE_MIDIVAL_PATCH_FILES_H

#include <inttypes.h>

typedef struct MDV_Sample {
     // in milliHz
    uint32_t low_freq;
    uint32_t high_freq;
    uint32_t root_freq;
     // in samples
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t envelope_rates [6];
    uint32_t envelope_offsets [6];
    uint8_t pan;
    uint8_t loop;
    uint8_t pingpong;
    uint16_t sample_rate;
    uint32_t data_size;
    int16_t* data;
} MDV_Sample;

typedef struct MDV_Patch {
     // TODO: do we need any more information?
    uint16_t volume;
    int8_t note;
    uint8_t n_samples;
    MDV_Sample* samples;
} MDV_Patch;

MDV_Patch* _mdv_load_patch (const char* filename);
void mdv_free_patch (MDV_Patch*);

void mdv_print_patch (MDV_Patch*);

typedef struct MDV_Bank {
    MDV_Patch* patches [128];
    MDV_Patch* drums [128];
} MDV_Bank;

void mdv_bank_init (MDV_Bank*);
void mdv_bank_load_config (MDV_Bank*, const char* filename);
void mdv_bank_load_patch (MDV_Bank*, uint8_t instrument, const char* filename);
void mdv_bank_load_drum (MDV_Bank*, uint8_t instrument, const char* filename);
void mdv_bank_free_patches (MDV_Bank*);

#endif
