#include "player.h"
#include "midi.h"

#define SAMPLE_RATE 48000

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

 // In MilliHz, between note 0 and note 12
uint32 freqs [256];
void init_freqs () {
    static int initted = 0;
    if (!initted) {
        initted = 1;
        for (uint32 i = 0; i < 256; i++) {
            freqs[i] = 440000 * pow(2.0, ((i*12.0/256.0) - 69) / 12.0);
        }
    }
}

 // Input: 8:8 fixed point
 // Output: frequency in milliHz
uint32 get_freq (uint16 note) {
    uint16 note2 = note / 12;
    return freqs[note2 % 256] << (note2 / 256);
}

typedef struct Voice {
    uint8 next;
    uint8 note;
    uint8 channel;
    uint8 velocity;
    uint8 sample_index;
    uint8 backwards;
    uint8 envelope_phase;
     // 15:15 (?) fixed point
    uint32 envelope_value;
     // 32:32 fixed point
    uint64 sample_pos;
} Voice;

typedef struct Channel {
     // TODO: a lot more controllers
    uint8 program;
    uint8 volume;
    uint8 expression;
    int8 pan;
    int16 pitch_bend;
} Channel;

struct Player {
     // Specification
    uint32 tick_length;
    Sequence* seq;
    Bank* bank;
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
        p->channels[i].pitch_bend = 0;
        p->channels[i].pan = 0;
    }
    p->active = 255;
    p->inactive = 0;
    for (uint32 i = 0; i < 255; i++) {
        p->voices[i].next = i + 1;
    }
}

Player* new_player () {
    init_freqs();
    Player* player = (Player*)malloc(sizeof(Player));
    reset_player(player);
    player->bank = NULL;
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
void set_bank (Player* player, Bank* bank) {
    player->bank = bank;
}

void do_event (Player* player, Event* event) {
    switch (event->type) {
        case NOTE_OFF: {
            do_note_off:
            if (event->channel != 9) {
                for (uint8 i = player->active; i != 255; i = player->voices[i].next) {
                    Voice* v = &player->voices[i];
                    if (v->channel == event->channel && v->note == event->param1) {
                        if (v->envelope_phase < 3) {
                            v->envelope_phase = 3;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case NOTE_ON: {
            if (event->param2 == 0)
                goto do_note_off;
            if (player->inactive != 255) {
                Voice* v = &player->voices[player->inactive];
                player->inactive = v->next;
                v->next = player->active;
                player->active = v - player->voices;
                v->channel = event->channel;
                v->note = event->param1;
                v->velocity = event->param2;
                v->backwards = 0;
                v->sample_pos = 0;
                v->sample_index = 0;
                v->envelope_phase = 0;
                v->envelope_value = 0;
                 // Decide which patch sample we're using
                Channel* ch = &player->channels[v->channel];
                if (player->bank) {
                    Patch* patch = v->channel == 9
                        ? player->bank->drums[v->note]
                        : player->bank->patches[ch->program];
                    if (patch) {
                        uint32 freq = get_freq(v->note << 8);
                        for (uint8 i = 0; i < patch->n_samples; i++) {
                            if (patch->samples[i].high_freq > freq) {
                                v->sample_index = i;
                                break;
                            }
                        }
                    }
                }
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
                case PAN:
                    player->channels[event->channel].pan = event->param2 - 64;
                    break;
                default:
                    break;
            }
            break;
        }
        case PROGRAM_CHANGE: {
             // Silence all voices in this channel.
            uint8* np = &player->active;
            while (*np != 255) {
                Voice* v = &player->voices[*np];
                if (v->channel == event->channel) {
                    *np = v->next;
                    v->next = player->inactive;
                    player->inactive = v - player->voices;
                }
                else {
                    np = &v->next;
                }
            }
            player->channels[event->channel].program = event->param1;
            break;
        }
        case PITCH_BEND: {
            player->channels[event->channel].pitch_bend =
                (event->param2 << 7 | event->param1) - 8192;
            break;
        }
        case SET_TEMPO: {
            uint32 ms_per_beat = event->channel << 16 | event->param1 << 8 | event->param2;
            player->tick_length = (uint64)SAMPLE_RATE * ms_per_beat / 1000000 / player->seq->tpb;
            break;
        }
        default:
            break;
    }
}

typedef struct Samp {
    int16 l;
    int16 r;
} Samp;

void get_audio (Player* player, uint8* buf_, int len) {
    Sequence* seq = player->seq;
    Samp* buf = (Samp*)buf_;
    len /= 4;  // Assuming always a whole number of samples
    if (!seq || player->done) {
        for (int i = 0; i < len; i++) {
            buf[i].l = 0;
            buf[i].r = 0;
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
        int32 left = 0;
        int32 right = 0;
        uint8* next_ip;
        for (uint8* ip = &player->active; *ip != 255; ip = next_ip) {
            Voice* v = &player->voices[*ip];
            next_ip = &v->next;
            goto skip_delete_voice;
            delete_voice: {
                next_ip = ip;
                *ip = v->next;
                v->next = player->inactive;
                player->inactive = v - player->voices;
                continue;
            }
            skip_delete_voice: { }
            Channel* ch = &player->channels[v->channel];
            if (player->bank) {
                Patch* patch = v->channel == 9
                    ? player->bank->drums[v->note]
                    : player->bank->patches[ch->program];
                if (patch) {
                    Sample* sample = &patch->samples[v->sample_index];
                     // Account for pitch bend
                    uint32 freq = get_freq(v->note * 256 + ch->pitch_bend / 16);
                     // Do envelopes  TODO: fade to 0 at end
                    uint32 rate = sample->envelope_rates[v->envelope_phase];
                    uint32 target = sample->envelope_offsets[v->envelope_phase];
                    if (target > v->envelope_value) {
                        if (v->envelope_value + rate < target) {
                            v->envelope_value += rate;
                        }
                        else if (v->envelope_phase == 5) {
                            goto delete_voice;
                        }
                        else {
                            v->envelope_value = target;
                            if (v->envelope_phase != 2) {
                                v->envelope_phase += 1;
                            }
                        }
                    }
                    else {
                        if (target + rate < v->envelope_value) {
                            v->envelope_value -= rate;
                        }
                        else if (v->envelope_phase == 5 || target == 0) {
                            goto delete_voice;
                        }
                        else {
                            v->envelope_value = target;
                            if (v->envelope_phase != 2) {
                                v->envelope_phase += 1;
                            }
                        }
                    }
                     // Calculate new position
                    uint64 next_pos;
                    if (v->backwards) {
                        next_pos = v->sample_pos - 0x100000000LL * sample->sample_rate / SAMPLE_RATE * freq / sample->root_freq;
                    }
                    else {
                        next_pos = v->sample_pos + 0x100000000LL * sample->sample_rate / SAMPLE_RATE * freq / sample->root_freq;
                    }
                     // Loop
                    if (sample->loop) {
                        if (v->backwards) {
                            if (next_pos <= sample->loop_start * 0x100000000LL) {
                                v->backwards = 0;
                                next_pos = 2 * sample->loop_start * 0x100000000LL - next_pos;
                            }
                        }
                        else {
                            if (v->sample_pos >= sample->loop_end * 0x100000000LL) {
                                if (sample->pingpong) {
                                    v->backwards = 1;
                                    next_pos = 2 * sample->loop_end * 0x100000000LL - next_pos;
                                }
                                else {
                                    next_pos -= (sample->loop_end - sample->loop_start) * 0x100000000LL;
                                }
                            }
                        }
                    }
                    else if (v->sample_pos >= sample->data_size * 0x100000000LL) {
                        goto delete_voice;
                    }
                     // Linear interpolation.  TODO: is always +1 the right thing?
                    int64 samp = sample->data[v->sample_pos / 0x100000000LL] * (0x100000000LL - (v->sample_pos & 0xffffffffLL));
                    samp += sample->data[v->sample_pos / 0x100000000LL + 1] * (v->sample_pos & 0xffffffffLL);
                     // TODO: make this volume calculation better and easier to understand
                    uint64 val = samp / 0x100000000LL * patch->volume * ch->volume / 127 * ch->expression / 127
                                                      * v->velocity / 127 / 127 * v->envelope_value / (0xff << 22) / 4;
                    left += val * (64 + ch->pan) / 64;
                    right += val * (64 - ch->pan) / 64;
                     // Move position
                    v->sample_pos = next_pos;
                    continue;
                }
            }
             // No bank or no patch
             // Loop
            v->sample_pos %= 0x100000000LL;
             // Add value
            int32 sign = v->sample_pos < 0x80000000LL ? -1 : 1;
            uint32 val = sign * v->velocity * ch->volume * ch->expression / (32*127);
            left += val;
            right += val;
             // Move position
            uint32 freq = get_freq(v->note << 8);
            v->sample_pos += 0x100000000LL * freq / 1000 / SAMPLE_RATE;
        }
        buf[i].l = left > 32767 ? 32767 : left < -32768 ? -32768 : left;
        buf[i].r = right > 32767 ? 32767 : right < -32768 ? -32768 : right;
    }
}

