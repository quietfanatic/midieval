#include "midieval.h"

#define CONTROL_UPDATE_INTERVAL 16

#include <stdio.h>
#include <stdlib.h>

#include "player_tables.c"

typedef struct Voice_Sample {
     // 32:32 fixed point, signed to make math easier
    int64_t inc;
    int64_t pos;
    MDV_Sample* sample;
} Voice_Sample;

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
    int32_t tremolo_sweep_position;
    int32_t tremolo_phase;
    int32_t vibrato_sweep_position;
    int32_t vibrato_phase;
    uint32_t channel_volume;  // Cached so it doesn't affect ending notes
    uint32_t volume;
     // 0:32
    uint32_t sample_mix;
    Voice_Sample samples [2];
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
    uint32_t seq_pos;
    uint32_t samples_to_tick;
    uint32_t ticks_to_event;
    Channel channels [16];
    uint8_t inactive;  // inactive voices
    uint8_t n_active_voices;
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
    p->n_active_voices = 0;
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
    fprintf(stderr, "Clip count: %llu\n", (long long unsigned)player->clip_count);
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

void mdv_load_config (MDV_Player* player, const char* filename) {
    mdv_bank_load_config(&player->bank, filename);
}
void mdv_load_patch (MDV_Player* player, uint8_t index, const char* filename) {
    mdv_bank_load_patch(&player->bank, index, filename);
}
void mdv_load_drum (MDV_Player* player, uint8_t index, const char* filename) {
    mdv_bank_load_drum(&player->bank, index, filename);
}

void mdv_play_event (MDV_Player* player, MDV_Event* event) {
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
                player->n_active_voices += 1;
                Voice* v = &player->voices[player->inactive];
                Channel* ch = &player->channels[event->channel];
                player->inactive = v->next;
                v->next = ch->voices;
                ch->voices = v - player->voices;
                v->note = event->param1;
                v->velocity = event->param2;
                v->backwards = 0;
                v->control_timer = 1;
                v->envelope_phase = 0;
                v->envelope_value = 0;
                v->tremolo_sweep_position = 0;
                v->tremolo_phase = 0;
                v->vibrato_sweep_position = 0;
                v->vibrato_phase = 0;
                v->sample_mix = 0;
                for (int i = 0; i < 2; i++) {
                    v->samples[i].inc = 0;
                    v->samples[i].pos = 0;
                    v->samples[i].sample = NULL;
                }
                 // Decide which patch sample we're using
                MDV_Patch* patch = event->channel == 9
                    ? player->bank.drums[v->note]
                    : player->bank.patches[ch->program];
                if (patch) {
                    v->patch_volume = patch->volume;
                    v->do_envelope = ch != player->channels + 9
                                  || patch->keep_envelope;
                    v->do_loop = ch != player->channels + 9
                              || patch->keep_loop;
                    if (patch->note >= 0)
                        v->note = patch->note;
                    uint32_t freq = get_freq(v->note * 0x10000);
                    uint32_t lower_freq = 0;
                    uint32_t upper_freq = -1;
                    for (uint8_t i = 0; i < patch->n_samples; i++) {
                        if (patch->samples[i].root_freq > lower_freq
                         && patch->samples[i].root_freq <= freq) {
                            v->samples[0].sample = &patch->samples[i];
                            lower_freq = patch->samples[i].root_freq;
                        }
                        if (patch->samples[i].root_freq < upper_freq
                         && patch->samples[i].root_freq >= freq) {
                            v->samples[1].sample = &patch->samples[i];
                            upper_freq = patch->samples[i].root_freq;
                        }
                    }
                    if (!v->samples[0].sample) {
                        v->samples[0].sample = v->samples[1].sample;
                        v->samples[1].sample = NULL;
                    }
                    else if (!v->samples[1].sample) {
                    }
                    else {
                         // Compare distance between notes, not frequencies.
                         // (the scale doesn't matter so we're just taking
                         //  any old logarithm and not worrying about it).
                        double low = log(v->samples[0].sample->root_freq);
                        double mid = log(freq);
                        double high = log(v->samples[1].sample->root_freq);
                        v->sample_mix = (mid - low) / (high - low)
                                      * 0x100000000LL;
                    }
                }
                else {
                    v->samples[0].inc = get_freq(v->note * 0x10000) * 0x10000 / MDV_SAMPLE_RATE;
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
                player->n_active_voices -= 1;
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

 // Delete voice if returns true
static int render_voice (Channel* ch, Voice* v, int32_t (* chunk )[2], int chunk_length) {
    if (v->samples[0].sample) {
        for (int i = 0; i < chunk_length; i++) {
             // Update volume and pitch only every once in a while
            if (!--v->control_timer) {
                v->control_timer = CONTROL_UPDATE_INTERVAL;
                 // Do envelopes  TODO: fade to 0 at end  TODO2: what did I mean when I wrote that
                if (v->do_envelope) {
                    uint32_t rate = v->samples[0].sample->envelope_rates[v->envelope_phase] * CONTROL_UPDATE_INTERVAL;
                    uint32_t target = v->samples[0].sample->envelope_offsets[v->envelope_phase];
                    if (v->sample_mix) {
                        uint32_t rate1 = v->samples[0].sample->envelope_rates[v->envelope_phase] * CONTROL_UPDATE_INTERVAL;
                        uint32_t target1 = v->samples[0].sample->envelope_offsets[v->envelope_phase];
                        rate = (
                            rate * (0x100000000LL - v->sample_mix)
                          + rate1 * (uint64_t)v->sample_mix
                        ) / 0x100000000LL;
                        target = (
                            target * (0x100000000LL - v->sample_mix)
                          + target1 * (uint64_t)v->sample_mix
                        ) / 0x100000000LL;
                    }
                    if (target > v->envelope_value) {  // Get louder
                        if (v->envelope_value + rate < target) {
                            v->envelope_value += rate;
                        }
                        else if (v->envelope_phase == 5) {
                            return 1;
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
                            return 1;
                        }
                        else {
                            v->envelope_value = target;
                            if (v->envelope_phase != 2) {
                                v->envelope_phase += 1;
                            }
                        }
                    }
                }
                else { v->envelope_value = 0x3ff00000; }
                 // Tremolo
                uint32_t ts = v->samples[0].sample->tremolo_sweep_increment * CONTROL_UPDATE_INTERVAL;
                uint32_t tp = v->samples[0].sample->tremolo_sweep_increment * CONTROL_UPDATE_INTERVAL;
                uint16_t td = v->samples[0].sample->tremolo_depth;
                if (v->sample_mix) {
                    uint32_t ts1 = v->samples[1].sample->tremolo_sweep_increment * CONTROL_UPDATE_INTERVAL;
                    uint32_t tp1 = v->samples[1].sample->tremolo_sweep_increment * CONTROL_UPDATE_INTERVAL;
                    uint16_t td1 = v->samples[1].sample->tremolo_depth;
                    ts = (
                        ts * (0x100000000LL - v->sample_mix)
                      + ts1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                    tp = (
                        tp * (0x100000000LL - v->sample_mix)
                      + tp1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                    td = (
                        td * (0x100000000LL - v->sample_mix)
                      + td1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                }
                v->tremolo_sweep_position += ts;
                if (v->tremolo_sweep_position > 0x1000000)
                    v->tremolo_sweep_position = 0x1000000;
                v->tremolo_phase += tp;
                if (v->tremolo_phase >= 0x1000000)
                    v->tremolo_phase -= 0x1000000;
                uint32_t tremolo = td
                                 * v->tremolo_sweep_position / (0x1000000 / 0x80)
                                 * sines[v->tremolo_phase / (0x1000000 / SINES_SIZE)] / 0x8000;
                 // Volume calculation.
                if (v->envelope_phase < 3) {
                    v->channel_volume = (uint32_t)vols[ch->volume]
                                      * vols[ch->expression] / 0x10000;
                }
                v->volume = (uint32_t)v->patch_volume * 0x80
                          * v->channel_volume / 0x10000
                          * vols[v->velocity] / 0x10000
                          * envs[v->envelope_value / 0x100000] / 0x10000
                          * (0x10000 + tremolo) / 0x10000;
                 // Vibrato
                uint32_t vs = v->samples[0].sample->vibrato_sweep_increment * CONTROL_UPDATE_INTERVAL;
                uint32_t vp = v->samples[0].sample->vibrato_sweep_increment * CONTROL_UPDATE_INTERVAL;
                uint16_t vd = v->samples[0].sample->vibrato_depth;
                if (v->sample_mix) {
                    uint32_t vs1 = v->samples[1].sample->vibrato_sweep_increment * CONTROL_UPDATE_INTERVAL;
                    uint32_t vp1 = v->samples[1].sample->vibrato_sweep_increment * CONTROL_UPDATE_INTERVAL;
                    uint16_t vd1 = v->samples[1].sample->vibrato_depth;
                    vs = (
                        vs * (0x100000000LL - v->sample_mix)
                      + vs1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                    vp = (
                        vp * (0x100000000LL - v->sample_mix)
                      + vp1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                    vd = (
                        vd * (0x100000000LL - v->sample_mix)
                      + vd1 * (uint64_t)v->sample_mix
                    ) / 0x100000000LL;
                }
                v->vibrato_sweep_position += vs;
                if (v->vibrato_sweep_position > 0x1000000)
                    v->vibrato_sweep_position = 0x1000000;
                v->vibrato_phase += vp;
                if (v->vibrato_phase >= 0x1000000)
                    v->vibrato_phase -= 0x1000000;
                uint32_t vibrato = vd
                                 * v->vibrato_sweep_position / (0x1000000 / 0x80)
                                 * sines[v->vibrato_phase / (0x1000000 / SINES_SIZE)] / 0x8000;
                 // Notes are on a logarithmic scale, so we add instead of multiplying
                uint32_t note = v->note * 0x10000
                              + ch->pitch_bend * 0x10
                              + vibrato * 4;  // Range over a whole step
                v->samples[0].inc = v->samples[0].sample->sample_inc
                                  * get_freq(note) / v->samples[0].sample->root_freq;
                if (v->sample_mix) {
                    v->samples[1].inc = v->samples[1].sample->sample_inc
                                      * get_freq(note) / v->samples[1].sample->root_freq;
                }
            }

             // Linear interpolation.
            uint32_t high = v->samples[0].pos / 0x100000000LL;
            uint64_t low = v->samples[0].pos % 0x100000000LL;
            int64_t samp = v->samples[0].sample->data[high] * (0x100000000LL - low)
                         + v->samples[0].sample->data[high + 1] * low;
            if (v->sample_mix) {
                uint32_t high1 = v->samples[1].pos / 0x100000000LL;
                uint64_t low1 = v->samples[1].pos % 0x100000000LL;
                int64_t samp1 = v->samples[1].sample->data[high1] * (0x100000000LL - low1)
                              + v->samples[1].sample->data[high1 + 1] * low1;
                samp = (
                    samp / 0x100000000LL * (0x100000000LL - v->sample_mix)
                  + samp1 / 0x100000000LL * (uint64_t)v->sample_mix
                );
            }
             // Write!
            uint64_t val = samp / 0x100000000LL * v->volume / 0x10000;
            chunk[i][0] += val * (64 + ch->pan) / 64;
            chunk[i][1] += val * (64 - ch->pan) / 64;
             // Move sample position forward (or backward)
             // TODO: go all the way to sample end if no loop
            if (v->backwards) {
                v->samples[0].pos -= v->samples[0].inc;
                if (v->samples[0].pos < v->samples[0].sample->loop_start) {
                    if (v->do_loop) {
                         // pingpong assumed
                        v->backwards = 0;
                        v->samples[0].pos = 2 * v->samples[0].sample->loop_start - v->samples[0].pos;
                    }
                    else return 1;
                }
            }
            else {
                v->samples[0].pos += v->samples[0].inc;
                if (v->samples[0].pos >= v->samples[0].sample->loop_end) {
                    if (v->do_loop) {
                        if (v->samples[0].sample->pingpong) {
                            v->backwards = 1;
                            v->samples[0].pos = 2 * v->samples[0].sample->loop_end - v->samples[0].pos;
                        }
                        else {
                            v->samples[0].pos -= v->samples[0].sample->loop_end - v->samples[0].sample->loop_start;
                        }
                    }
                    else return 1;
                }
            }
            if (v->sample_mix) {
                if (v->backwards) {
                    v->samples[1].pos -= v->samples[1].inc;
                    if (v->samples[1].pos < v->samples[1].sample->loop_start) {
                        if (v->do_loop) {
                             // pingpong assumed
                            v->backwards = 0;
                            v->samples[1].pos = 2 * v->samples[1].sample->loop_start - v->samples[1].pos;
                        }
                        else return 1;
                    }
                }
                else {
                    v->samples[1].pos += v->samples[1].inc;
                    if (v->samples[1].pos >= v->samples[1].sample->loop_end) {
                        if (v->do_loop) {
                            if (v->samples[1].sample->pingpong) {
                                v->backwards = 1;
                                v->samples[1].pos = 2 * v->samples[1].sample->loop_end - v->samples[1].pos;
                            }
                            else {
                                v->samples[1].pos -= v->samples[1].sample->loop_end - v->samples[1].sample->loop_start;
                            }
                        }
                        else return 1;
                    }
                }
            }
        }
    }
    else {  // No patch, do a square wave!
        for (int i = 0; i < chunk_length; i++) {
             // Loop
            v->samples[0].pos %= 0x100000000LL;
             // Add value
            int32_t sign = v->samples[0].pos < 0x80000000LL ? -1 : 1;
            uint32_t val = sign * v->velocity * ch->volume * ch->expression / (32*127);
            chunk[i][0] += val;
            chunk[i][1] += val;
             // Move position
            v->samples[0].pos += v->samples[0].inc;
        }
    }
    return 0;
}

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
             // Indirection so we can delete from linked list while we traverse it.
            uint8_t* vip = &ch->voices;
            while (*vip != 255) {
                Voice* v = &player->voices[*vip];
                int do_delete = render_voice(ch, v, chunk, chunk_length);
                if (do_delete) {
                    *vip = v->next;
                    v->next = player->inactive;
                    player->inactive = v - player->voices;
                    player->n_active_voices -= 1;
                }
                else {
                    vip = &v->next;
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

