
#include <math.h>

 // In 16:16 Hz, between note 0 and note 12
#define FREQS_SIZE 4096
static uint32_t freqs [FREQS_SIZE];
static void init_freqs () {
    for (uint32_t i = 0; i < FREQS_SIZE; i++) {
        freqs[i] = 440 * 0x10000 * pow(2.0, ((i*12.0/FREQS_SIZE) - 69) / 12.0);
    }
}
 // Input: 16:16 fixed point
 // Output: frequency in 16:16 Hz
static uint32_t get_freq (uint32_t note) {
    uint32_t octave = note / 12 / 0x10000;
    uint32_t fraction = note / 12 % 0x10000;
    return freqs[fraction * FREQS_SIZE / 0x10000] << octave;
}

 // Using magic value 1.66096404744 stolen from TiMidity source
static uint16_t vols [128];
static void init_vols () {
    for (uint8_t i = 0; i < 128; i++) {
        vols[i] = 65535 * pow(i / 127.0, 1.66096404744);
    }
}

#define SINES_SIZE 1024
static int16_t sines [SINES_SIZE];
static void init_sines () {
    for (int16_t i = 0; i < SINES_SIZE; i++) {
        sines[i] = sin(2 * 3.14159265358979 * i / SINES_SIZE) * 0x7fff;
    }
}

#define ENVS_SIZE 1024
static uint16_t envs [ENVS_SIZE];
static void init_envs () {
    for (uint16_t i = 0; i < ENVS_SIZE; i++) {
        envs[i] = pow(2.0, (i * 1.0 / (ENVS_SIZE - 1) - 1) * 6) * 0xffff;
    }
}

static void init_tables () {
    static int initted = 0;
    if (!initted) {
        initted = 1;
        init_freqs();
        init_vols();
        init_sines();
        init_envs();
    }
}
