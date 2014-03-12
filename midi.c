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
void read_event (uint8** p, uint8* end, Timed_Event* event, uint8* status) {
    event->delta += read_var(p, end);
    uint8 byte = *(*p)++;
    if (byte == 0xff) {
        Meta_Event* me = &event->event.meta_event;
        me->type = 0xff;
        if (end - *p < 1) {
            fprintf(stderr, "Premature end of track during meta event header\n");
        }
        me->meta_type = *(*p)++;
        me->data_size = read_var(p, end);
        if (end - *p < me->data_size) {
            fprintf(stderr, "Premature end of track during meta event sized 0x%x\n", me->data_size);
            exit(1);
        }
        me->data = *p;
        *p += me->data_size;
    }
    else {
        Channel_Event* ce = &event->event.channel_event;
        if (byte & 0x80) {
            ce->type = byte >> 4;
            ce->channel = byte & 0x0f;
            ce->param1 = *(*p)++;
            *status = byte;
        }
        else {
            ce->type = *status >> 4;
            ce->channel = *status & 0x0f;
            ce->param1 = byte;
        }
        if (end - *p < 2) {
            fprintf(stderr, "Premature end of track during channel event.\n");
            exit(1);
        }
        if (parameters_used(ce->type) == 2)
            ce->param2 = *(*p)++;
        else
            ce->param2 = 0;
    }
}

int cmp_event (const void* a_, const void* b_) {
    Timed_Event* a = (Timed_Event*)a_;
    Timed_Event* b = (Timed_Event*)b_;
    return (a->delta > b->delta) - (a->delta < b->delta);
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
    m->events = NULL;
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
     // Read all events into an array
    size_t max_events = 128;
    m->events = malloc(max_events * sizeof(Timed_Event));
    m->n_events = 0;

    for (size_t i = 0; i < m->n_tracks; i++) {
        uint8* p = m->tracks[i].data;
        uint8* end = p + m->tracks[i].data_size;
        size_t time = 0;
        uint8 status = 0x80;  // Doesn't really matter
        while (p != end) {
            if (m->n_events >= max_events) {
                max_events *= 2;
                m->events = realloc(m->events, max_events * sizeof(Timed_Event));
            }
            read_event(&p, end, &m->events[m->n_events], &status);
             // Cheat with the time to make sorting work
             // This will break MIDI files that are too long
             // (207 days at tpb=120 & tempo=120)
            time += m->events[m->n_events].delta;
            m->events[m->n_events].delta = time;
            m->n_events += 1;
        }
    }
     // Now sort them by time
    qsort(m->events, m->n_events, sizeof(Timed_Event), cmp_event);
     // Now fix the time delta cheat
    size_t time = 0;
    for (size_t i = 0; i < m->n_events; i++) {
        uint32 new_time = m->events[i].delta;
        m->events[i].delta -= time;
        time = new_time;
    }

    return m;
  fail:
    free_midi(m);
    exit(1);
}

void free_midi (Midi* m) {
    if (m->tracks)
        free(m->tracks);
    free(m->data);
    if (m->events)
        free(m->events);
    free(m);
}

void debug_print_midi (Midi* m) {
    printf("%lu\n", (unsigned long)m->n_tracks);
    for (size_t i = 0; i < m->n_tracks; i++) {
        printf("%lu: %lu\n", i, m->tracks[i].data_size);
    }
    for (size_t i = 0; i < m->n_events; i++) {
        printf("%08X ", m->events[i].delta);
        print_event(&m->events[i].event);
    }
}
