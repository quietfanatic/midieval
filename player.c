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
    uint8 next;
    uint8 note;
    uint8 channel;
    uint8 velocity;
    uint32 phase;
} Voice;

typedef struct Channel {
     // TODO: a lot more controllers
    uint8 volume;
    uint8 expression;
} Channel;

struct Player {
     // Specification
    uint32 tick_length;
    Sequence* seq;
     // State
    Timed_Event* current;
    uint32 samples_to_tick;
    uint32 ticks_to_event;
    int done;
    Channel channels [16];
    uint8 active;
    uint8 inactive;
    Voice voices [255];
};

void reset_player (Player* p) {
    for (uint32 i = 0; i < 16; i++) {
        p->channels[i].volume = 127;
        p->channels[i].expression = 127;
    }
    p->active = 255;
    p->inactive = 0;
    for (uint32 i = 0; i < 255; i++) {
        p->voices[i].next = i + 1;
    }
}

Player* new_player () {
    init_wavelengths();
    Player* player = (Player*)malloc(sizeof(Player));
    reset_player(player);
    return player;
}
void free_player (Player* player) {
    free(player);
}

void play_sequence (Player* player, Sequence* seq) {
     // Default tempo is 120bpm
    player->tick_length = SAMPLE_RATE / seq->tpb / 2;
    player->seq = seq;
    player->current = seq->events;
    player->samples_to_tick = player->tick_length;
    player->ticks_to_event = seq->events[0].time;
    player->done = 0;
}

void do_event (Player* player, Event* event) {
    switch (event->type) {
        case NOTE_OFF: {
            uint8* np = &player->active;
            while (*np != 255) {
                Voice* v = &player->voices[*np];
                if (v->channel == event->channel && v->note == event->param1) {
                    *np = v->next;
                    v->next = player->inactive;
                    player->inactive = v - player->voices;
                }
                else {
                    np = &v->next;
                }
            }
            break;
        }
        case NOTE_ON: {
            uint8* np = &player->active;
            while (*np != 255) {
                Voice* v = &player->voices[*np];
                if (v->channel == event->channel && v->note == event->param1) {
                    *np = v->next;
                    v->next = player->inactive;
                    player->inactive = v - player->voices;
                }
                else {
                    np = &v->next;
                }
            }
            if (event->param2 && player->inactive != 255) {
                Voice* v = &player->voices[player->inactive];
                player->inactive = v->next;
                v->next = player->active;
                player->active = v - player->voices;
                v->channel = event->channel;
                v->note = event->param1;
                v->velocity = event->param2;
            }
            break;
        }
        case CONTROLLER: {
            switch (event->param1) {
                case VOLUME:
                    player->channels[event->channel].volume = event->param2;
                    break;
                case EXPRESSION:
                    player->channels[event->channel].expression = event->param2;
                    break;
                default:
                    break;
            }
            break;
        }
        case SET_TEMPO: {
            uint32 ms_per_beat = event->channel << 16 | event->param1 << 8 | event->param2;
            player->tick_length = (uint64)SAMPLE_RATE * ms_per_beat / 1000000 / player->seq->tpb;
        }
        default:
            break;
    }
}

void get_audio (Player* player, uint8* buf_, int len) {
    Sequence* seq = player->seq;
    int16* buf = (int16*)buf_;
    len /= 2;  // Assuming always an even number
    if (!seq || player->done) {
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
                uint32 old_time = player->current->time;
                player->current += 1;
                if (player->current == seq->events + seq->n_events) {
                    player->done = 1;
                }
                else {
                    player->ticks_to_event = player->current->time - old_time;
                }
            }
            --player->ticks_to_event;
            player->samples_to_tick = player->tick_length;
        }
         // Now mix voices
        int32 val = 0;
        for (uint8 i = player->active; i != 255; i = player->voices[i].next) {
            Voice* v = &player->voices[i];
            uint32 wl = wavelengths[v->note];
            Channel* ch = &player->channels[v->channel];
            val += (v->phase < wl / 2 ? -v->velocity : v->velocity) * ch->volume * ch->expression / (32*127);
            v->phase += 1;
            v->phase %= wl;
        }
        buf[i] = val > 32767 ? 32767 : val < -32768 ? -32768 : val;
    }
}

