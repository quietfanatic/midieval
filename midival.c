
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

#define PI 3.141592653589793
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

 // Voices

typedef struct Voice {
    struct Voice* prev;
    struct Voice* next;
    int16 phase;
    int16 wavelength;
    int16 volume;
     // Currently, mere existence implies a note
} Voice;

typedef struct Voices {
    Voice* prev;
    Voice* next;
} Voices;

Voices voices = {(Voice*)&voices, (Voice*)&voices};

void add_voice (uint16 wavelength, uint16 volume, uint16 phase) {
    Voice* v = (Voice*)malloc(sizeof(Voice));
    v->next = (Voice*)&voices;
    v->prev = voices.prev;
    voices.prev->next = v;
    voices.prev = v;
    v->phase = phase;
    v->volume = volume;
    v->wavelength = wavelength;
}
void remove_voice (Voice* v) {
    v->prev->next = v->prev;
    v->next->prev = v->next;
}

void fill_audio (void* dat_, uint8* buf_, int len) {
    int16* buf = (int16*)buf_;
    len /= 2;  // Assuming always an even number
    for (int i = 0; i < len; i++) {
        int32 val = 0;
        for (Voice* v = voices.next; v != (Voice*)&voices; v = v->next) {
            val += v->phase < v->wavelength / 2 ? -v->volume : v->volume;
            v->phase += 1;
            v->phase %= v->wavelength;
        }
        buf[i] = val > 32767 ? 32767 : val < -32768 ? -32768 : val;
    }
}

 // MIDI Handling

typedef struct Track {
    size_t data_size;
    uint8* data;
} Track;

typedef struct Midi {
    uint16 tpb;
    uint16 n_tracks;
    Track* tracks;
    size_t data_size;
    uint8* data;
} Midi;

void free_midi (Midi* m) {
    if (m->tracks)
        free(m->tracks);
    free(m->data);
    free(m);
}

uint32 read_u32 (uint8* data) {
    return data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
}
uint16 read_u16 (uint8* data) {
    return data[0] << 8 | data[1];
}

Midi* load_midi (const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s for reading: %s\n", filename, strerror(errno));
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8* data = (uint8*)malloc(size);
    if (fread(data, 1, size, f) != size) {
        fprintf(stderr, "Failed to read from %s: %s\n", filename, strerror(errno));
        exit(1);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "Failed to close %s: %s\n", filename, strerror(errno));
        exit(1);
    }
    Midi* m = malloc(sizeof(Midi));
    m->tracks = NULL;
    m->data_size = size;
    m->data = data;
    if (size < 22) {
        fprintf(stderr, "This file is not nearly long enough to be a MIDI file! (%lu)\n", size);
        goto fail;
    }
    uint32 magic = read_u32(data);
    if (magic != read_u32((uint8*)"MThd")) {
        fprintf(stderr, "This file is not a MIDI file (Magic number = %08lx).\n", (unsigned long)magic);
        goto fail;
    }
    m->n_tracks = read_u16(data + 10);
    m->tpb = read_u16(data + 12);
    if (m->tpb & 0x8000) {
        fprintf(stderr, "This program cannot recognize SMTPE-format time divisions.\n");
        goto fail;
    }
    m->tracks = (Track*)malloc(m->n_tracks * sizeof(Track));
    size_t pos = 14;
    for (size_t i = 0; i < m->n_tracks; i++) {
        if (size - pos < 8) {
            fprintf(stderr, "Premature end of file during chunk header %lu.\n", i);
            goto fail;
        }
        uint32 chunk_id = read_u32(data + pos);
        if (chunk_id != read_u32((uint8*)"MTrk")) {
            fprintf(stderr, "Wrong chunk ID %08lx).\n", (unsigned long)chunk_id);
            goto fail;
        }
        uint32 chunk_size = read_u32(data + pos + 4);
        pos += 8;
        if (size - pos < chunk_size) {
            fprintf(stderr, "Premature end of file during track %lu.\n", i);
            goto fail;
        }
        m->tracks[i].data_size = chunk_size;
        m->tracks[i].data = data + pos;
        pos += chunk_size;
    }
    if (pos != size) {
        fprintf(stderr, "Warning: extra junk at end of MIDI file.\n");
    }

    return m;
  fail:
    free_midi(m);
    exit(1);
}

int main () {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
    }

    SDL_AudioSpec spec;
    spec.freq = 48000;
    spec.format = AUDIO_S16;
    spec.channels = 1;
    spec.samples = 4096;
    spec.callback = fill_audio;
    spec.userdata = NULL;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    if (dev == 0) {
        printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    }
    SDL_PauseAudioDevice(dev, 0);
    Midi* m = load_midi("test.mid");
    printf("%lu\n", (unsigned long)m->n_tracks);
    for (size_t i = 0; i < m->n_tracks; i++) {
        printf("%lu: %lu\n", i, m->tracks[i].data_size);
    }
    add_voice(300, 2000, 0);
    fgetc(stdin);
    SDL_Quit();
    return 0;
}
