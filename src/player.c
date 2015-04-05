#include "midieval.h"

#define VOLUME_UPDATE_INTERVAL 16
#define PITCH_UPDATE_INTERVAL 16

#include <stdio.h>
#include <stdlib.h>

#include "player_tables.c"

typedef struct Voice {
    uint8_t next;
    uint8_t note;
    uint8_t velocity;
    uint8_t sample_index;
    uint8_t backwards;
    uint8_t envelope_phase;
    uint8_t volume_timer;
    uint8_t pitch_timer;
     // 15:15 (?) fixed point
    uint32_t envelope_value;
     // 8:24
    int32_t tremolo_sweep_position;
    int32_t tremolo_phase;
    int32_t vibrato_sweep_position;
    int32_t vibrato_phase;
    uint32_t channel_volume;  // Cached so it doesn't affect ending notes
    uint32_t volume;
    int64_t sample_inc;
     // 32:32 fixed point
     // Signed to make math easier
    int64_t sample_pos;
    MDV_Patch* patch;  // Giving up and inserting this
} Voice;

typedef struct Channel {
     // TODO: a lot more controllers
    uint8_t program;
    uint8_t volume;
    uint8_t expression;
    int8_t pan;
    int16_t pitch_bend;
    uint8_t voices;
     // Usually true for drum patches
    uint8_t no_envelope;
    uint8_t no_loop;
} Channel;

struct MDV_Player {
     // Specification
    uint32_t tick_length;
    MDV_Sequence* seq;
    MDV_Bank bank;
     // State
    MDV_Timed_Event* current;
    uint32_t samples_to_tick;
    uint32_t ticks_to_event;
    int done;
    Channel channels [16];
    uint8_t inactive;  // inactive voices
    Voice voices [255];
     // Debug
    uint64_t clip_count;
};

void mdv_reset_player (MDV_Player* p) {
    for (uint32_t i = 0; i < 16; i++) {
        p->channels[i].volume = 127;
        p->channels[i].expression = 127;
        p->channels[i].pitch_bend = 0;
        p->channels[i].pan = 0;
        p->channels[i].voices = 255;
    }
    p->inactive = 0;
    for (uint32_t i = 0; i < 255; i++) {
        p->voices[i].next = i + 1;
    }
    p->clip_count = 0;
}

FILE* debug_f;

MDV_Player* mdv_new_player () {
    init_tables();
    debug_f = fopen("debug_out", "w");
    MDV_Player* player = (MDV_Player*)malloc(sizeof(MDV_Player));
    mdv_reset_player(player);
    mdv_bank_init(&player->bank);
    return player;
}
void mdv_free_player (MDV_Player* player) {
    mdv_bank_free_patches(&player->bank);
    fprintf(stderr, "Clip count: %llu\n", player->clip_count);
    free(player);
}

void mdv_play_sequence (MDV_Player* player, MDV_Sequence* seq) {
     // Default tempo is 120bpm
    player->tick_length = MDV_SAMPLE_RATE / seq->tpb / 2;
    player->seq = seq;
    player->current = seq->events;
    player->samples_to_tick = player->tick_length;
    player->ticks_to_event = seq->events[0].time;
    player->done = 0;
}

int mdv_currently_playing (MDV_Player* player) {
    return player->seq && !player->done;
}

void mdv_load_config (MDV_Player* player, const char* filename) {
    mdv_bank_load_config(&player->bank, filename);
}
void mdv_load_patch (MDV_Player* player, uint8_t index, const char* filename) {
    mdv_bank_load_patch(&player->bank, index, filename);
}
void mdv_load_drum (MDV_Player* player, uint8_t index, const char* filename) {
    mdv_bank_load_drum(&player->bank, index, filename);
}

static void do_event (MDV_Player* player, MDV_Event* event) {
    switch (event->type) {
        case MDV_NOTE_OFF: {
            do_note_off:
            if (event->channel != 9) {
                for (uint8_t i = player->channels[event->channel].voices; i != 255; i = player->voices[i].next) {
                    Voice* v = &player->voices[i];
                    if (v->note == event->param1) {
                        if (v->envelope_phase < 3) {
                            v->envelope_phase = 3;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case MDV_NOTE_ON: {
            if (event->param2 == 0)
                goto do_note_off;
            if (player->inactive != 255) {
                Voice* v = &player->voices[player->inactive];
                Channel* ch = &player->channels[event->channel];
                player->inactive = v->next;
                v->next = ch->voices;
                ch->voices = v - player->voices;
                v->note = event->param1;
                v->velocity = event->param2;
                v->backwards = 0;
                v->volume_timer = 1;
                v->pitch_timer = 1;
                v->sample_pos = 0;
                v->sample_index = 0;
                v->envelope_phase = 0;
                v->envelope_value = 0;
                v->tremolo_sweep_position = 0;
                v->tremolo_phase = 0;
                v->vibrato_sweep_position = 0;
                v->vibrato_phase = 0;
                 // Decide which patch sample we're using
                v->patch = event->channel == 9
                    ? player->bank.drums[v->note]
                    : player->bank.patches[ch->program];
                if (v->patch) {
                    if (v->patch->note >= 0)
                        v->note = v->patch->note;
                    uint32_t freq = get_freq(v->note * 0x10000);
                    for (uint8_t i = 0; i < v->patch->n_samples; i++) {
                        if (v->patch->samples[i].high_freq > freq) {
                            v->sample_index = i;
                            break;
                        }
                    }
                }
            }
            break;
        }
        case MDV_CONTROLLER: {
            switch (event->param1) {
                case MDV_VOLUME:
                    player->channels[event->channel].volume = event->param2;
                    break;
                case MDV_EXPRESSION:
                    player->channels[event->channel].expression = event->param2;
                    break;
                case MDV_PAN:
                    player->channels[event->channel].pan = event->param2 - 64;
                    break;
                default:
                    break;
            }
            break;
        }
        case MDV_PROGRAM_CHANGE: {
             // Silence all voices in this channel.
            Channel* ch = &player->channels[event->channel];
            uint8_t* np = &ch->voices;
            while (*np != 255) {
                Voice* v = &player->voices[*np];
                *np = v->next;
                v->next = player->inactive;
                player->inactive = v - player->voices;
            }
            ch->program = event->param1;
            break;
        }
        case MDV_PITCH_BEND: {
            player->channels[event->channel].pitch_bend =
                (event->param2 << 7 | event->param1) - 8192;
            break;
        }
        case MDV_SET_TEMPO: {
            uint32_t ms_per_beat = event->channel << 16 | event->param1 << 8 | event->param2;
            player->tick_length = (uint64_t)MDV_SAMPLE_RATE * ms_per_beat / 1000000 / player->seq->tpb;
            break;
        }
        default:
            break;
    }
}

typedef struct Samp {
    int16_t l;
    int16_t r;
} Samp;

void mdv_fast_forward_to_note (MDV_Player* player) {
    if (!player->seq) return;
    player->samples_to_tick = 1;
    player->ticks_to_event = 0;
    while (!player->done && player->current->event.type != MDV_NOTE_ON) {
        do_event(player, &player->current->event);
        player->current += 1;
        if (player->current >= player->seq->events + player->seq->n_events) {
            player->done = 1;
        }
    }
}

#define MAX_CHUNK_LENGTH 512

void mdv_get_audio (MDV_Player* player, uint8_t* buf_, int len) {
    MDV_Sequence* seq = player->seq;
    int16_t(* buf )[2] = (int16_t(*)[2])buf_;
    len /= 4;  // Assuming always a whole number of samples
    if (!seq || player->done) {
        for (int i = 0; i < len; i++) {
            buf[i][0] = 0;
            buf[i][1] = 0;
        }
        return;
    }
    int buf_pos = 0;
    while (buf_pos < len) {
     // Advance event timeline.
        if (!player->samples_to_tick) {
            while (!player->done && !player->ticks_to_event) {
                do_event(player, &player->current->event);
                uint32_t old_time = player->current->time;
                player->current += 1;
                if (player->current >= seq->events + seq->n_events) {
                    player->done = 1;
                }
                else {
                    player->ticks_to_event = player->current->time - old_time;
                }
            }
            if (!player->done)
                --player->ticks_to_event;
            player->samples_to_tick = player->tick_length;
        }
        int chunk_length = player->samples_to_tick < len - buf_pos
                         ? player->samples_to_tick : len - buf_pos;
        if (chunk_length > MAX_CHUNK_LENGTH)
            chunk_length = MAX_CHUNK_LENGTH;
        player->samples_to_tick -= chunk_length;

         // Mix voices a whole chunk at a time.  This is better for the CPU cache.
        int32_t chunk [chunk_length][2];
        for (int i = 0; i < chunk_length; i++) {
            chunk[i][0] = 0;
            chunk[i][1] = 0;
        }
        for (Channel* ch = player->channels+0; ch < player->channels+16; ch++) {
             // A bunch of pointer shuffling for the linked list
            uint8_t* next_ip;
            for (uint8_t* ip = &ch->voices; *ip != 255; ip = next_ip) {
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
                uint8_t no_envelope = 0;
                uint8_t no_loop = 0;
                if (ch == player->channels+9) {
                    no_envelope = !v->patch->keep_envelope;
                    no_loop = !v->patch->keep_loop;
                }
                if (v->patch) {
                    MDV_Sample* sample = &v->patch->samples[v->sample_index];
                    for (int i = 0; i < chunk_length; i++) {
                        if (!--v->volume_timer) {
                            v->volume_timer = VOLUME_UPDATE_INTERVAL;
                             // Do envelopes  TODO: fade to 0 at end
                            if (no_envelope) {
                                v->envelope_value = 0x3ff00000;
                            }
                            else {
                                uint32_t rate = sample->envelope_rates[v->envelope_phase] * VOLUME_UPDATE_INTERVAL;
                                uint32_t target = sample->envelope_offsets[v->envelope_phase];
                                if (target > v->envelope_value) {  // Get louder
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
                                else {  // Get quieter
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
                            }
                             // Tremolo
                            v->tremolo_sweep_position += sample->tremolo_sweep_increment * VOLUME_UPDATE_INTERVAL;
                            if (v->tremolo_sweep_position > 0x1000000)
                                v->tremolo_sweep_position = 0x1000000;
                            v->tremolo_phase += sample->tremolo_phase_increment * VOLUME_UPDATE_INTERVAL;
                            if (v->tremolo_phase >= 0x1000000)
                                v->tremolo_phase -= 0x1000000;
                            uint32_t tremolo = sample->tremolo_depth
                                             * v->tremolo_sweep_position / (0x1000000 / 0x80)
                                             * sines[v->tremolo_phase / (0x1000000 / SINES_SIZE)] / 0x8000;

                             // Volume calculation.
                            if (v->envelope_phase < 3) {
                                v->channel_volume = (uint32_t)vols[ch->volume]
                                                  * vols[ch->expression] / 0x10000;
                            }
                            v->volume = (uint32_t)v->patch->volume * 0x80
                                      * v->channel_volume / 0x10000
                                      * vols[v->velocity] / 0x10000
                                      * envs[v->envelope_value / 0x100000] / 0x10000
                                      * (0x10000 + tremolo) / 0x10000;
                        }
                         // Linear interpolation.
                        uint32_t high = v->sample_pos / 0x100000000LL;
                        uint64_t low = v->sample_pos % 0x100000000LL;
                        int64_t samp = sample->data[high] * (0x100000000LL - low)
                                     + sample->data[high + 1] * low;
                         // Write!
                        uint64_t val = samp / 0x100000000LL * v->volume / 0x10000;
                        chunk[i][0] += val * (64 + ch->pan) / 64;
                        chunk[i][1] += val * (64 - ch->pan) / 64;

                         // Vibrato
                        if (!--v->pitch_timer) {
                            v->pitch_timer = PITCH_UPDATE_INTERVAL;
                            v->vibrato_sweep_position += sample->vibrato_sweep_increment * PITCH_UPDATE_INTERVAL;
                            if (v->vibrato_sweep_position > 0x1000000)
                                v->vibrato_sweep_position = 0x1000000;
                            v->vibrato_phase += sample->vibrato_phase_increment * PITCH_UPDATE_INTERVAL;
                            if (v->vibrato_phase >= 0x1000000)
                                v->vibrato_phase -= 0x1000000;
                            uint32_t vibrato = sample->vibrato_depth
                                             * v->vibrato_sweep_position / (0x1000000 / 0x80)
                                             * sines[v->vibrato_phase / (0x1000000 / SINES_SIZE)] / 0x8000;

                             // Notes are on a logarithmic scale, so we add instead of multiplying
                            uint32_t note = v->note * 0x10000
                                          + ch->pitch_bend * 0x10
                                          + vibrato * 4;  // Range over a whole step
                            v->sample_inc = sample->sample_inc
                                          * get_freq(note) / sample->root_freq;
                        }
                         // Move sample position forward (or backward)
                        if (v->backwards) {
                            v->sample_pos -= v->sample_inc;
                            if (v->sample_pos <= sample->loop_start * 0x100000000LL) {
                                if (sample->loop && !no_loop) {
                                     // pingpong assumed
                                    v->backwards = 0;
                                    v->sample_pos = 2 * sample->loop_start * 0x100000000LL - v->sample_pos;
                                }
                                else goto delete_voice;
                            }
                        }
                        else {
                            v->sample_pos += v->sample_inc;
                            if (v->sample_pos >= sample->loop_end * 0x100000000LL) {
                                if (sample->loop && !no_loop) {
                                    if (sample->pingpong) {
                                        v->backwards = 1;
                                        v->sample_pos = 2 * sample->loop_end * 0x100000000LL - v->sample_pos;
                                    }
                                    else {
                                        v->sample_pos -= (sample->loop_end - sample->loop_start) * 0x100000000LL;
                                    }
                                }
                                else goto delete_voice;
                            }
                        }
                    }
                }
                else {  // No patch, do a square wave!
                    for (int i = 0; i < chunk_length; i++) {
                         // Loop
                        v->sample_pos %= 0x100000000LL;
                         // Add value
                        int32_t sign = v->sample_pos < 0x80000000LL ? -1 : 1;
                        uint32_t val = sign * v->velocity * ch->volume * ch->expression / (32*127);
                        chunk[i][0] += val;
                        chunk[i][1] += val;
                         // Move position
                        uint32_t freq = get_freq(v->note << 8);
                        v->sample_pos += 0x100000000LL * freq / 1000 / MDV_SAMPLE_RATE;
                    }
                }
            }
        }
         // Finally write the chunk to buffer
        for (int i = 0; i < chunk_length; i++) {
            buf[buf_pos][0] = chunk[i][0] > 32767 ? 32767 : chunk[i][0] < -32768 ? -32768 : chunk[i][0];
            buf[buf_pos][1] = chunk[i][1] > 32767 ? 32767 : chunk[i][1] < -32768 ? -32768 : chunk[i][1];
             // debug clip count
            if (buf[buf_pos][0] == 32767 || buf[buf_pos][0] == -32768)
                player->clip_count += 1;
            if (buf[buf_pos][1] == 32767 || buf[buf_pos][1] == -32768)
                player->clip_count += 1;
            buf_pos += 1;
        }
    }
}

