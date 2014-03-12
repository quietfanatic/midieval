#ifndef MIDIVAL_MIDI_H
#define MIDIVAL_MIDI_H

#include <stdlib.h>
#include "events.h"

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
    size_t n_events;
    Timed_Event* events;
} Midi;

Midi* load_midi (const char* filename);
void free_midi (Midi*);

void debug_print_midi (Midi*);

#endif
