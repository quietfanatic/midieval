#include "midi.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "events.h"

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

void debug_print_midi (Midi* m) {
    printf("%lu\n", (unsigned long)m->n_tracks);
    for (size_t i = 0; i < m->n_tracks; i++) {
        printf("%lu: %lu\n", i, m->tracks[i].data_size);
    }
}
