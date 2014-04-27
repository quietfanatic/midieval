#include "midival.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_u32 (uint8_t* data) {
    return data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
}
static uint16_t read_u16 (uint8_t* data) {
    return data[0] << 8 | data[1];
}
static uint32_t read_var (uint8_t** p, uint8_t* end) {
    uint32_t r = 0;
    uint8_t byte;
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

static int cmp_event (const void* a_, const void* b_) {
    MDV_Timed_Event* a = (MDV_Timed_Event*)a_;
    MDV_Timed_Event* b = (MDV_Timed_Event*)b_;
    return (a->time > b->time) - (a->time < b->time);
}

MDV_Sequence* mdv_load_midi (const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s for reading: %s\n", filename, strerror(errno));
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(size);
    uint8_t* file_end = data + size;
    uint8_t* p = data;
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
    uint32_t magic = read_u32(p);
    p += 4;
    if (magic != read_u32((uint8_t*)"MThd")) {
        fprintf(stderr, "This file is not a MIDI file (Magic number = %08lx).\n", (unsigned long)magic);
        exit(1);
    }
    p += 6;  // Skipping some stuff we don't care about
    uint16_t n_tracks = read_u16(p);
    p += 2;
    uint16_t tpb = read_u16(p);
    p += 2;
    if (tpb & 0x8000) {
        fprintf(stderr, "This program cannot recognize SMTPE-format time divisions.\n");
        exit(1);
    }
    MDV_Sequence* seq = malloc(sizeof(MDV_Sequence));
    seq->tpb = tpb;
    size_t max_events = 256;
    seq->events = malloc(max_events * sizeof(MDV_Timed_Event));
    seq->n_events = 0;

    for (uint16_t i = 0; i < n_tracks; i++) {
         // Verify track header
        if (file_end - p < 8) {
            fprintf(stderr, "Premature end of file during chunk header %hu of %hu.\n", i, n_tracks);
            goto fail;
        }
        uint32_t chunk_id = read_u32(p);
        p += 4;
        if (chunk_id != read_u32((uint8_t*)"MTrk")) {
            fprintf(stderr, "Wrong chunk ID %08lx).\n", (unsigned long)chunk_id);
            goto fail;
        }
        uint32_t chunk_size = read_u32(p);
        p += 4;
        if (file_end - p < chunk_size) {
            fprintf(stderr, "Premature end of file during track %hu.\n", i);
            goto fail;
        }
         // Load track's events
        uint8_t* end = p + chunk_size;
        uint32_t time = 0;
        uint8_t status = 0x80;  // Doesn't really matter
        while (p != end) {
            if (seq->n_events >= max_events) {
                max_events *= 2;
                seq->events = realloc(seq->events, max_events * sizeof(MDV_Timed_Event));
            }
            uint32_t delta = read_var(&p, end);
            time += delta;
            seq->events[seq->n_events].time = time;
            MDV_Event* ev = &seq->events[seq->n_events].event;
            if (end - p < 1) goto premature_end;
             // Optional type/channel byte
            uint8_t byte = *p;
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
                    uint8_t meta_type = *p++;
                    uint32_t size = read_var(&p, end);
                    if (end - p < size) goto premature_end;
                     // Set Tempo
                    if (meta_type == 0x51) {
                        if (size != 3) {
                            fprintf(stderr, "Tempo event was of incorrect size\n");
                            goto fail;
                        }
                        ev->type = MDV_SET_TEMPO;
                        ev->channel = p[0];
                        ev->param1 = p[1];
                        ev->param2 = p[2];
                        seq->n_events += 1;
                    }
                     // Otherwise ignore
                    p += size;
                }
                 // Ignore SYSEX
                else {
                    uint32_t size = read_var(&p, end);
                    if (end - p < size) goto premature_end;
                    p += size;
                }
            }
             // Normal event
            else {
                if (end - p < mdv_parameters_used(ev->type)) goto premature_end;
                ev->param1 = *p++;
                if (mdv_parameters_used(ev->type) == 2)
                    ev->param2 = *p++;
                else
                    ev->param2 = 0;
                seq->n_events += 1;
            }
        }
    }
    if (p != file_end) {
        fprintf(stderr, "Warning: extra junk at end of MIDI file.\n");
    }

     // Don't really need to do this, but it helps valgrind analysis
    seq->events = realloc(seq->events, seq->n_events * sizeof(MDV_Timed_Event));
     // Now sort the events by time
    qsort(seq->events, seq->n_events, sizeof(MDV_Timed_Event), cmp_event);

    free(data);
    return seq;
  premature_end:
    fprintf(stderr, "Premature end of track while parsing event\n");
  fail:
    mdv_free_sequence(seq);
    exit(1);
}

