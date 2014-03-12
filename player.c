#include "player.h"
#include "midi.h"

#define SAMPLE_RATE 48000

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

uint32 wavelengths [128];
void init_wavelengths () {
    static int initted = 0;
    if (!initted) {
        initted = 1;
        for (uint8 i = 0; i < 128; i++) {
            wavelengths[i] = SAMPLE_RATE / (440 * pow(2.0, (i - 69) / 12.0));
        }
    }
}

typedef struct Voice {
    struct Voice* prev;
    struct Voice* next;
    uint32 phase;
    uint8 note;
    uint8 channel;
    uint8 velocity;
} Voice;
typedef struct Voice_List {
    Voice* last;
    Voice* first;
} Voice_List;
void init_voice_list (Voice_List* l) {
    l->last = (Voice*)l;
    l->first = (Voice*)l;
}
void unlink_voice (Voice* v) {
    v->next->prev = v->prev;
    v->prev->next = v->next;
}
void link_voice (Voice* v, Voice_List* l) {
    v->next = (Voice*)l;
    v->prev = l->last;
    l->last->next = v;
    l->last = v;
}

#define MAX_VOICES 64

struct Player {
     // Voice management
    Voice_List active;
    Voice_List inactive;
    Voice voices [MAX_VOICES];
     // Specification
    uint32 tick_length;
    Midi* midi;
     // State
    Timed_Event* current;
    uint32 samples_to_tick;
    uint32 ticks_to_event;
    int done;
};

Player* new_player () {
    Player* player = (Player*)malloc(sizeof(Player));
    init_voice_list(&player->active);
    init_voice_list(&player->inactive);
    init_wavelengths();
    for (size_t i = 0; i < MAX_VOICES; i++) {
        link_voice(&player->voices[i], &player->inactive);
    }
    return player;
}
void free_player (Player* player) {
    free(player);
}

void play_midi (Player* player, Midi* m) {
     // Default tempo is 120bpm
    player->tick_length = SAMPLE_RATE / m->tpb / 2;
    player->midi = m;
    player->current = m->events;
    player->samples_to_tick = player->tick_length;
    player->ticks_to_event = m->events[0].delta;
    player->done = 0;
}

void do_event (Player* player, Event* event) {
    print_event(event);
    Meta_Event* me = &event->meta_event;
    Channel_Event* ce = &event->channel_event;
    switch (event->type) {
        case NOTE_OFF: {
            Voice* next_v;
            for (Voice* v = player->active.first; v != (Voice*)&player->active; v = next_v) {
                next_v = v->next;
                if (v->channel == ce->channel && v->note == ce->param1) {
                    unlink_voice(v);
                    link_voice(v, &player->inactive);
                }
            }
            break;
        }
        case NOTE_ON: {
            Voice* next_v;
            for (Voice* v = player->active.first; v != (Voice*)&player->active; v = next_v) {
                next_v = v->next;
                if (v->channel == ce->channel && v->note == ce->param1) {
                    unlink_voice(v);
                    link_voice(v, &player->inactive);
                }
            }
            if (player->inactive.first != (Voice*)&player->inactive) {
                Voice* v = player->inactive.first;
                unlink_voice(v);
                link_voice(v, &player->active);
                v->channel = ce->channel;
                v->note = ce->param1;
                v->velocity = ce->param2;
            }
            break;
        }
        case META: {
            if (me->meta_type == SET_TEMPO) {
                uint32 ms_per_beat = me->data[0] << 16 | me->data[1] << 8 | me->data[0];
                player->tick_length = (uint64)SAMPLE_RATE * ms_per_beat / 1000000 / player->midi->tpb;
            }
        }
        default:
            break;
    }
}

void get_audio (Player* player, uint8* buf_, int len) {
    Midi* midi = player->midi;
    int16* buf = (int16*)buf_;
    len /= 2;  // Assuming always an even number
    if (!midi || player->done) {
        for (int i = 0; i < len; i++) {
            buf[i] = 0;
        }
        return;
    }
    for (int i = 0; i < len; i++) {
         // Advance event timeline
        if (!--player->samples_to_tick) {
            while (!player->ticks_to_event) {
                do_event(player, &player->current->event);
                player->current += 1;
                if (player->current == midi->events + midi->n_events) {
                    player->done = 1;
                }
                else {
                    player->ticks_to_event = player->current->delta;
                }
            }
            --player->ticks_to_event;
            player->samples_to_tick = player->tick_length;
        }
         // Now mix voices
        int32 val = 0;
        for (Voice* v = player->active.first; v != (Voice*)&player->active; v = v->next) {
            uint32 wl = wavelengths[v->note];
            val += v->phase < wl / 2 ? -v->velocity : v->velocity;
            v->phase += 1;
            v->phase %= wl;
        }
        buf[i] = val > 32767 ? 32767 : val < -32768 ? -32768 : val;
    }
}

