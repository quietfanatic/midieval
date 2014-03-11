#ifndef MIDIVAL_MIDI_H
#define MIDIVAL_MIDI_H

typedef struct Midi Midi;

Midi* load_midi (const char* filename);
void free_midi (Midi*);

void debug_print_midi (Midi*);

#endif
