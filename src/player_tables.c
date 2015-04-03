
#include <math.h>

 // In MilliHz, between note 0 and note 12
static uint16_t freqs [256];
static void init_freqs () {
    for (uint32_t i = 0; i < 256; i++) {
        freqs[i] = 440000 * pow(2.0, ((i*12.0/256.0) - 69) / 12.0);
    }
}
 // Using magic value 1.66096404744 stolen from TiMidity source
static uint16_t vols [128];
static void init_vols () {
    for (uint8_t i = 0; i < 128; i++) {
        vols[i] = 65535 * pow(i / 127.0, 1.66096404744);
    }
}

 // Input: 8:8 fixed point
 // Output: frequency in milliHz
static uint32_t get_freq (uint16_t note) {
    uint16_t note2 = note / 12;
    return freqs[note2 % 256] << (note2 / 256);
}

static float sines [1024];
static void init_sines () {
    for (uint16_t i = 0; i < 1024; i++) {
        sines[i] = sin(2 * 3.14159265358979 * i / 1024);
    }
}

static float pows [1024];
static void init_pows () {
    for (uint16_t i = 0; i < 1024; i++) {
        pows[i] = pow(2.0, (i / 1023.0 - 1) * 6);
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
