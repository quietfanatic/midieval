
#include <math.h>

 // In 16:16 Hz, between note 0 and note 12
#define FREQ_TABLE_SIZE 4096
static uint32_t freqs [FREQ_TABLE_SIZE];
static void init_freqs () {
    for (uint32_t i = 0; i < FREQ_TABLE_SIZE; i++) {
        freqs[i] = 440 * 0x10000 * pow(2.0, ((i*12.0/FREQ_TABLE_SIZE) - 69) / 12.0);
    }
}
 // Input: 16:16 fixed point
 // Output: frequency in 16:16 Hz
static uint32_t get_freq (uint32_t note) {
    uint32_t octave = note / 12 / 0x10000;
    uint32_t fraction = note / 12 % 0x10000;
    return freqs[fraction * FREQ_TABLE_SIZE / 0x10000] << octave;
}

 // Using magic value 1.66096404744 stolen from TiMidity source
static uint16_t vols [128];
static void init_vols () {
    for (uint8_t i = 0; i < 128; i++) {
        vols[i] = 65535 * pow(i / 127.0, 1.66096404744);
    }
}

static int16_t sines [1024];
static void init_sines () {
    for (int16_t i = 0; i < 1024; i++) {
        sines[i] = sin(2 * 3.14159265358979 * i / 1024) * 0x7fff;
    }
}

static uint16_t pows [1024];
static void init_pows () {
    for (uint16_t i = 0; i < 1024; i++) {
        pows[i] = pow(2.0, (i / 1023.0 - 1) * 6) * 0xffff;
    }
}

static void init_tables () {
    static int initted = 0;
    if (!initted) {
        initted = 1;
        init_freqs();
        init_vols();
        init_sines();
        init_pows();
    }
}
