#include "midi.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "events.h"

uint32 read_u32 (uint8* data) {
    return data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
}
uint16 read_u16 (uint8* data) {
    return data[0] << 8 | data[1];
}
uint32 read_var (uint8** p, uint8* end) {
    uint32 r = 0;
    uint8 byte;
    do {
        if (*p == end) {
            fprintf(stderr, "Premature end of track during variable-length number\n");
            exit(1);
        }
        byte = *(*p)++;
        r <<= 7;
        r |= byte & 0x7f;
    } while (byte & 0x80);
    return r;
}

int cmp_event (const void* a_, const void* b_) {
    Timed_Event* a = (Timed_Event*)a_;
    Timed_Event* b = (Timed_Event*)b_;
    return (a->time > b->time) - (a->time < b->time);
}

Sequence* load_midi (const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s for reading: %s\n", filename, strerror(errno));
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8* data = (uint8*)malloc(size);
    uint8* file_end = data + size;
    uint8* p = data;
    if (fread(data, 1, size, f) != size) {
        fprintf(stderr, "Failed to read from %s: %s\n", filename, strerror(errno));
        exit(1);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "Failed to close %s: %s\n", filename, strerror(errno));
        exit(1);
    }
    if (size < 22) {
        fprintf(stderr, "This file is not nearly long enough to be a MIDI file! (%lu)\n", size);
        exit(1);
    }
    uint32 magic = read_u32(p);
    p += 4;
    if (magic != read_u32((uint8*)"MThd")) {
        fprintf(stderr, "This file is not a MIDI file (Magic number = %08lx).\n", (unsigned long)magic);
        exit(1);
    }
    p += 6;  // Skipping some stuff we don't care about
    uint16 n_tracks = read_u16(p);
    p += 2;
    uint16 tpb = read_u16(p);
    p += 2;
    if (tpb & 0x8000) {
        fprintf(stderr, "This program cannot recognize SMTPE-format time divisions.\n");
        exit(1);
    }
    Sequence* seq = malloc(sizeof(Sequence));
    seq->tpb = tpb;
    size_t max_events = 256;
    seq->events = malloc(max_events * sizeof(Timed_Event));
    seq->n_events = 0;

    for (uint16 i = 0; i < n_tracks; i++) {
         // Verify track header
        if (file_end - p < 8) {
            fprintf(stderr, "Premature end of file during chunk header %hu of %hu.\n", i, n_tracks);
            goto fail;
        }
        uint32 chunk_id = read_u32(p);
        p += 4;
        if (chunk_id != read_u32((uint8*)"MTrk")) {
            fprintf(stderr, "Wrong chunk ID %08lx).\n", (unsigned long)chunk_id);
            goto fail;
        }
        uint32 chunk_size = read_u32(p);
        p += 4;
        if (file_end - p < chunk_size) {
            fprintf(stderr, "Premature end of file during track %hu.\n", i);
            goto fail;
        }
         // Load track's events
        uint8* end = p + chunk_size;
        uint32 time = 0;
        uint8 status = 0x80;  // Doesn't really matter
        while (p != end) {
            if (seq->n_events >= max_events) {
                max_events *= 2;
                seq->events = realloc(seq->events, max_events * sizeof(Timed_Event));
            }
            uint32 delta = read_var(&p, end);
            time += delta;
            seq->events[seq->n_events].time = time;
            Event* ev = &seq->events[seq->n_events].event;
            printf("%p\n", ev);
            if (end - p < 1) goto premature_end;
             // Optional type/channel byte
            uint8 byte = *p;
            if (byte & 0x80) {
                ev->type = byte >> 4;
                ev->channel = byte & 0x0f;
                status = byte;
                p++;
            }
            else {
                ev->type = status >> 4;
                ev->channel = status & 0x0f;
            }
             // Special event
            if (ev->type == 0x0f) {
                 // Meta event
                if (ev->channel == 0x0f) {
                    if (end - p < 1) goto premature_end;
                    uint8 meta_type = *p++;
                    uint32 size = read_var(&p, end);
                    if (end - p < size) goto premature_end;
                     // Set Tempo
                    if (meta_type == 0x51) {
                        if (size != 3) {
                            printf("Tempo event was of incorrect size\n");
                            goto fail;
                        }
                        ev->channel = p[0];
                        ev->param1 = p[1];
                        ev->param2 = p[2];
                        print_event(ev);
                        seq->n_events += 1;
                    }
                     // Otherwise ignore
                    p += size;
                }
                 // Ignore SYSEX
                else {
                    uint32 size = read_var(&p, end);
                    if (end - p < size) goto premature_end;
                    p += size;
                }
            }
             // Normal event
            else {
                if (end - p < parameters_used(ev->type)) goto premature_end;
                ev->param1 = *p++;
                if (parameters_used(ev->type) == 2)
                    ev->param2 = *p++;
                else
                    ev->param2 = 0;
                print_event(ev);
                seq->n_events += 1;
            }
        }
    }
    if (p != file_end) {
        fprintf(stderr, "Warning: extra junk at end of MIDI file.\n");
    }

     // Now sort the events by time
    qsort(seq->events, seq->n_events, sizeof(Timed_Event), cmp_event);

    return seq;
  premature_end:
    printf("Premature end of track while parsing event\n");
  fail:
    free_sequence(seq);
    exit(1);
}

