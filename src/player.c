#include "midieval.h"

#define CONTROL_UPDATE_INTERVAL 16

#include <stdio.h>
#include <stdlib.h>

#include "player_tables.c"

typedef struct Voice {
    uint8_t next;
    uint8_t note;
    uint8_t velocity;
    uint8_t backwards;
    uint8_t envelope_phase;
    uint8_t control_timer;
    uint8_t patch_volume;
    uint8_t do_envelope;
    uint8_t do_loop;
     // 15:15 (?) fixed point
    uint32_t envelope_value;
     // 8:24
    int32_t tremolo_sweep;
    int32_t tremolo_phase;
    int32_t vibrato_sweep;
    int32_t vibrato_phase;
    uint32_t channel_volume;  // Cached so it doesn't affect ending notes
    uint32_t volume;
    int64_t sample_inc;
     // 32:32 fixed point
     // Signed to make math easier
    int64_t sample_pos;
    MDV_Sample* sample;
} Voice;

typedef struct Channel {
     // TODO: a lot more controllers
    uint16_t rpn;
    int32_t pitch_bend_sensitivity;  // 16:16 in half steps
    int16_t pitch_bend;  // 0:14, as per midi spec
    uint8_t volume;
    uint8_t expression;
    int8_t pan;
    uint8_t voices;
    uint8_t no_envelope;  // Usually true for drum patches
    uint8_t no_loop;  // ''
    uint8_t is_drums;
    uint8_t bank;
    MDV_Patch* patch;  // Because bank changing doesn't affect this
} Channel;


struct MDV_Player {
     // Specification
    uint8_t n_banks;
    uint8_t n_drumsets;
    MDV_Patch*** banks;  // You read that right, three stars
    MDV_Patch*** drumsets;
    uint32_t tick_length;
    MDV_Sequence* seq;
     // State
    uint32_t seq_pos;
    uint32_t samples_to_tick;
    uint32_t ticks_to_event;
    Channel channels [16];
    uint8_t inactive;  // inactive voices
    uint8_t n_active_voices;
    Voice voices [255];
     // Debug
    uint64_t clip_count;
    int32_t max_value;
};

void mdv_channel_set_drums (MDV_Player* p, uint8_t channel, int is_drums) {
    if (channel < 16)
        p->channels[channel].is_drums = is_drums;
}
int mdv_channel_is_drums (MDV_Player* p, uint8_t channel) {
    if (channel < 16)
        return p->channels[channel].is_drums;
    else
        return 0;
}

FILE* debug_f;

MDV_Player* mdv_new_player () {
    init_tables();
    debug_f = fopen("debug_out", "w");
    MDV_Player* player = (MDV_Player*)malloc(sizeof(MDV_Player));
    player->n_banks = 0;
    player->banks = NULL;
    player->n_drumsets = 0;
    player->drumsets = NULL;
    player->clip_count = 0;
    player->max_value = 0;
    MDV_Event reset = {MDV_COMMON, MDV_RESET, 0, 0};
    mdv_play_event(player, &reset);
    return player;
}
void mdv_free_player (MDV_Player* player) {
    for (uint8_t i = 0; i < player->n_banks; i++) {
        for (uint8_t j = 0; j < 128; j++)
            mdv_patch_free(player->banks[i][j]);
        free(player->banks[i]);
    }
    free(player->banks);
    for (uint8_t i = 0; i < player->n_drumsets; i++) {
        for (uint8_t j = 0; j < 128; j++)
            mdv_patch_free(player->drumsets[i][j]);
        free(player->drumsets[i]);
    }
    free(player->drumsets);
    fprintf(stderr, "Clip count: %llu\n", (long long unsigned)player->clip_count);
    fprintf(stderr, "Max value: %08lx\n", (long unsigned)player->max_value);
    free(player);
}

void mdv_play_sequence (MDV_Player* player, MDV_Sequence* seq) {
     // Default tempo is 120bpm
    player->tick_length = MDV_SAMPLE_RATE / seq->tpb / 2;
    player->seq = seq;
    player->seq_pos = 0;
    player->samples_to_tick = player->tick_length;
    player->ticks_to_event = seq->events[0].time;
}

int mdv_currently_playing (MDV_Player* player) {
    return player->seq
        && (player->seq_pos < player->seq->n_events
         || player->n_active_voices > 0);
}

void mdv_set_patch (MDV_Player* player, uint8_t bank, uint8_t program, MDV_Patch* patch) {
    if (bank+1 > player->n_banks) {
        player->banks = realloc(player->banks, (bank+1) * sizeof(MDV_Patch**));
        for (uint8_t i = player->n_banks; i < bank; i++)
            player->banks[i] = NULL;
        player->n_banks = bank + 1;
        player->banks[bank] = malloc(128 * sizeof(MDV_Patch*));
        for (uint8_t i = 0; i < 128; i++) {
            player->banks[bank][i] = NULL;
        }
    }
     // TODO: only turn off what we need to
    for (uint8_t i = 0; i < 16; i++) {
        MDV_Event e = {MDV_CONTROLLER, i, MDV_ALL_SOUND_OFF, 0};
        mdv_play_event(player, &e);
    }
    if (player->banks[bank][program])
        mdv_patch_free(player->banks[bank][program]);
    player->banks[bank][program] = patch;
}
void mdv_set_drum (MDV_Player* player, uint8_t bank, uint8_t program, MDV_Patch* patch) {
    if (bank+1 > player->n_drumsets) {
        player->drumsets = realloc(player->drumsets, (bank+1) * sizeof(MDV_Patch**));
        for (uint8_t i = player->n_drumsets; i < bank; i++)
            player->drumsets[i] = NULL;
        player->n_drumsets = bank + 1;
        player->drumsets[bank] = malloc(128 * sizeof(MDV_Patch*));
        for (uint8_t i = 0; i < 128; i++) {
            player->drumsets[bank][i] = NULL;
        }
    }
     // TODO: only turn off what we need to
    for (uint8_t i = 0; i < 16; i++) {
        MDV_Event e = {MDV_CONTROLLER, i, MDV_ALL_SOUND_OFF, 0};
        mdv_play_event(player, &e);
    }
    if (player->drumsets[bank][program])
        mdv_patch_free(player->drumsets[bank][program]);
    player->drumsets[bank][program] = patch;
}

void mdv_play_event (MDV_Player* player, MDV_Event* event) {
    if (event->channel > 16) return;
    Channel* ch = &player->channels[event->channel];
    switch (event->type) {
        case MDV_NOTE_OFF: {
            do_note_off:
            if (!ch->is_drums) {
                for (uint8_t i = ch->voices; i != 255; i = player->voices[i].next) {
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
                player->n_active_voices += 1;
                Voice* v = &player->voices[player->inactive];
                player->inactive = v->next;
                v->next = ch->voices;
                ch->voices = v - player->voices;
                v->note = event->param1;
                v->velocity = event->param2;
                v->backwards = 0;
                v->control_timer = 1;
                v->sample_pos = 0;
                v->envelope_phase = 0;
                v->envelope_value = 0;
                v->tremolo_sweep = 0;
                v->tremolo_phase = 0;
                v->vibrato_sweep = 0;
                v->vibrato_phase = 0;
                 // Decide which patch sample we're using
                MDV_Patch* patch = ch->is_drums
                    ? ch->bank < player->n_drumsets ? player->drumsets[ch->bank][v->note] : NULL
                    : ch->patch;
                if (patch) {
                    v->patch_volume = patch->volume;
                    v->do_envelope = !ch->is_drums || patch->keep_envelope;
                    v->do_loop = !ch->is_drums || patch->keep_loop;
                    uint32_t freq = get_freq(v->note * 0x10000);
                    v->sample = &patch->samples[0];
                    for (uint8_t i = 0; i < patch->n_samples; i++) {
                        if (patch->samples[i].high_freq > freq) {
                            v->sample = &patch->samples[i];
                            break;
                        }
                    }
                    if (patch->note >= 0)
                        v->note = patch->note;
                    else if (v->sample->scale_factor != 1024) {
                         // TODO: I guess this means that v->note needs to be 16-bit.
                        v->note += (v->note - v->sample->scale_note)
                                 * (v->sample->scale_factor - 1024) / 1024;
                    }
                }
                else {
                    v->sample = NULL;
                }
            }
            break;
        }
        case MDV_CONTROLLER: {
            switch (event->param1) {
                case MDV_BANK_SELECT:
                    ch->bank = event->param2;
                    break;
                case MDV_DATA_ENTRY_MSB:
                    if (ch->rpn == 0x0000)
                        ch->pitch_bend_sensitivity =
                            ch->pitch_bend_sensitivity % 0x10000
                          + event->param2 * 0x10000;
                    break;
                case MDV_DATA_ENTRY_LSB:
                    if (ch->rpn == 0x0000)
                        ch->pitch_bend_sensitivity =
                            ch->pitch_bend_sensitivity / 0x10000 * 0x10000
                          + (event->param2 <= 99 ? event->param2 : 99) * 0x10000 / 100;
                    break;
                case MDV_VOLUME:
                    ch->volume = event->param2;
                    break;
                case MDV_EXPRESSION:
                    ch->expression = event->param2;
                    break;
                case MDV_PAN:
                    ch->pan = event->param2 - 64;
                    break;
                case MDV_RPN_LSB:
                    ch->rpn = (ch->rpn & 0x3f80) | (event->param2 & 0x7f);
                    break;
                case MDV_RPN_MSB:
                    ch->rpn = (ch->rpn & 0x007f) | ((event->param2 << 7) & 0x3f80);
                    break;
                case MDV_ALL_SOUND_OFF:
                    for (uint8_t i = ch->voices; i != 255; i = player->voices[i].next)
                        player->n_active_voices -= 1;
                    ch->voices = 255;
                    break;
                case MDV_ALL_CONTROLLERS_OFF:
                    ch->rpn = 0x3fff;
                    ch->pitch_bend_sensitivity = 0x20000;
                    ch->pitch_bend = 0;
                    ch->volume = 127;
                    ch->expression = 127;
                    ch->pan = 0;
                    ch->bank = 0;
                    break;
                case MDV_ALL_NOTES_OFF:
                    for (uint8_t i = ch->voices; i != 255; i = player->voices[i].next) {
                        if (player->voices[i].envelope_phase < 3)
                            player->voices[i].envelope_phase = 3;
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        case MDV_PROGRAM_CHANGE: {
            ch->patch = ch->bank < player->n_banks
                ? player->banks[ch->bank][event->param1]
                : NULL;
            break;
        }
        case MDV_PITCH_BEND: {
            ch->pitch_bend = (event->param2 << 7 | event->param1) - 0x2000;
            break;
        }
        case MDV_COMMON: {
            switch (event->channel) {  // actually common event type, not channel
                case MDV_RESET: {
                    for (Channel* ch = player->channels; ch < player->channels + 16; ch++) {
                        ch->rpn = 0x3fff;
                        ch->pitch_bend_sensitivity = 0x20000;
                        ch->pitch_bend = 0;
                        ch->volume = 127;
                        ch->expression = 127;
                        ch->pan = 0;
                        ch->voices = 255;
                        ch->is_drums = 0;
                        ch->patch = NULL;
                    }
                    player->channels[9].is_drums = 1;
                    player->inactive = 0;
                    player->n_active_voices = 0;
                    for (uint32_t i = 0; i < 255; i++) {
                        player->voices[i].next = i + 1;
                    }
                    break;
                }
                default: break;
            }
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
    MDV_Sequence* seq = player->seq;
    if (!seq) return;
    while (player->seq_pos < seq->n_events
        && seq->events[player->seq_pos].event.type != MDV_NOTE_ON
    ) {
        mdv_play_event(player, &seq->events[player->seq_pos].event);
        player->seq_pos += 1;
    }
    player->samples_to_tick = 0;
    player->ticks_to_event = 0;
}

#define MAX_CHUNK_LENGTH 512

void mdv_get_audio (MDV_Player* player, uint8_t* buf_, int len) {
    int16_t(* buf )[2] = (int16_t(*)[2])buf_;
    len /= 4;  // Assuming always a whole number of samples
    if (!mdv_currently_playing(player)) {
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
            MDV_Sequence* seq = player->seq;
            while (player->seq_pos < seq->n_events && !player->ticks_to_event) {
                MDV_Timed_Event* te = &seq->events[player->seq_pos];
                mdv_play_event(player, &te->event);
                player->seq_pos += 1;
                if (player->seq_pos < seq->n_events) {
                    player->ticks_to_event = seq->events[player->seq_pos].time - te->time;
                }
            }
            if (player->ticks_to_event)
                player->ticks_to_event -= 1;
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
                    player->n_active_voices -= 1;
                    continue;
                }
                skip_delete_voice: { }
                if (v->sample) {
                    for (int i = 0; i < chunk_length; i++) {
                         // Update volume and pitch only every once in a while
                        if (!--v->control_timer) {
                            v->control_timer = CONTROL_UPDATE_INTERVAL;
                             // Do envelopes  TODO: fade to 0 at end  TODO2: what did I mean when I wrote that
                            if (v->do_envelope) {
                                uint32_t rate = v->sample->envelope_rates[v->envelope_phase] * CONTROL_UPDATE_INTERVAL;
                                uint32_t target = v->sample->envelope_offsets[v->envelope_phase];
                                if (target > v->envelope_value) {  // Get louder
                                    if (v->envelope_value + rate < target) {
                                        v->envelope_value += rate;
                                    }
                                    else if (v->envelope_phase == 5) {
                                        goto delete_voice;
                                    }
                                    else {
                                        v->envelope_value = target;
                                        if (v->envelope_phase != 2 || !v->sample->sustain) {
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
                                        if (v->envelope_phase != 2 || !v->sample->sustain) {
                                            v->envelope_phase += 1;
                                        }
                                    }
                                }
                            }
                            else { v->envelope_value = 0x3ff00000; }
                             // Tremolo
                            v->tremolo_sweep += v->sample->tremolo_sweep_inc * CONTROL_UPDATE_INTERVAL;
                            if (v->tremolo_sweep > 0x1000000)
                                v->tremolo_sweep = 0x1000000;
                            v->tremolo_phase += v->sample->tremolo_phase_inc * CONTROL_UPDATE_INTERVAL;
                            if (v->tremolo_phase >= 0x1000000)
                                v->tremolo_phase -= 0x1000000;
                            uint32_t tremolo = v->sample->tremolo_depth
                                             * v->tremolo_sweep / (0x1000000 / 0x80)
                                             * sines[v->tremolo_phase / (0x1000000 / SINES_SIZE)] / 0x8000;
                             // Volume calculation.
                            if (v->envelope_phase < 3) {
                                v->channel_volume = (uint32_t)vols[ch->volume]
                                                  * vols[ch->expression] / 0x10000;
                            }
                            v->volume = (uint32_t)v->patch_volume * 0x100
                                      * v->channel_volume / 0x10000
                                      * vols[v->velocity] / 0x10000
                                      * envs[v->envelope_value / 0x100000] / 0x10000
                                      * (0x10000 + tremolo) / 0x10000;
                             // Vibrato
                            v->vibrato_sweep += v->sample->vibrato_sweep_inc * CONTROL_UPDATE_INTERVAL;
                            if (v->vibrato_sweep > 0x1000000)
                                v->vibrato_sweep = 0x1000000;
                            v->vibrato_phase += v->sample->vibrato_phase_inc * CONTROL_UPDATE_INTERVAL;
                            if (v->vibrato_phase >= 0x1000000)
                                v->vibrato_phase -= 0x1000000;
                            uint32_t vibrato = v->sample->vibrato_depth
                                             * v->vibrato_sweep / (0x1000000 / 0x80)
                                             * sines[v->vibrato_phase / (0x1000000 / SINES_SIZE)] / 0x8000;
                             // Notes are on a logarithmic scale, so we add instead of multiplying
                            uint32_t note = (int64_t)v->note * 0x10000
                                          + (int64_t)ch->pitch_bend * ch->pitch_bend_sensitivity / 0x2000
                                          + vibrato * 4;  // Range over a whole step
                            v->sample_inc = v->sample->sample_inc
                                          * get_freq(note) / v->sample->root_freq;
                        }

                         // Linear interpolation.
                        uint32_t high = v->sample_pos / 0x100000000LL;
                        uint64_t low = v->sample_pos % 0x100000000LL;
                        int64_t samp = v->sample->data[high] * (0x100000000LL - low)
                                     + v->sample->data[high + 1] * low;
                         // Write!
                        uint64_t val = samp / 0x100000000LL * v->volume / 0x10000;
                        chunk[i][0] += val * (64 + ch->pan) / 64;
                        chunk[i][1] += val * (64 - ch->pan) / 64;
                         // Move sample position forward (or backward)
                         // TODO: go all the way to sample end if no loop
                        if (v->backwards) {
                            v->sample_pos -= v->sample_inc;
                            if (v->sample_pos < v->sample->loop_start) {
                                if (v->do_loop) {
                                     // pingpong assumed
                                    v->backwards = 0;
                                    v->sample_pos = 2 * v->sample->loop_start - v->sample_pos;
                                }
                                else goto delete_voice;
                            }
                        }
                        else {
                            v->sample_pos += v->sample_inc;
                            if (v->sample_pos >= v->sample->loop_end) {
                                if (v->do_loop) {
                                    if (v->sample->pingpong) {
                                        v->backwards = 1;
                                        v->sample_pos = 2 * v->sample->loop_end - v->sample_pos;
                                    }
                                    else {
                                        v->sample_pos -= v->sample->loop_end - v->sample->loop_start;
                                    }
                                }
                                else goto delete_voice;
                            }
                        }
                    }
                }
                else if (!ch->is_drums) {  // No patch, do a square wave!
                    if (v->envelope_phase >= 3)
                        goto delete_voice;
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
            if (chunk[i][0] > player->max_value)
                player->max_value = chunk[i][0];
            else if (-chunk[i][0] > player->max_value)
                player->max_value = -chunk[i][0];
            if (chunk[i][1] > player->max_value)
                player->max_value = chunk[i][1];
            else if (-chunk[i][1] > player->max_value)
                player->max_value = -chunk[i][1];
            buf_pos += 1;
        }
    }
}

