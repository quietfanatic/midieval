#ifndef MIDIEVAL_H
#define MIDIEVAL_H

#include <inttypes.h>

 // I can't guarantee values larger than this won't cause overflow somewhere
#define MDV_SAMPLE_RATE 48000

typedef struct MDV_Sequence MDV_Sequence;
typedef struct MDV_Player MDV_Player;
typedef struct MDV_Event MDV_Event;

///// Main player API /////

 // Allocate new player
MDV_Player* mdv_new_player ();
 // Load a .cfg containing patch names (nothing complicated please)
void mdv_load_config (MDV_Player*, const char* filename);
 // Set the sequence currently being played (use load_midi)
void mdv_play_sequence (MDV_Player*, MDV_Sequence*);
 // Just do a single event.
void mdv_play_event (MDV_Player*, MDV_Event*);
 // Get this many bytes of audio.  len must be a multiple of 4
void mdv_get_audio (MDV_Player*, uint8_t* buf, int len);
 // 0 if either no sequence was given or the sequence is done
int mdv_currently_playing (MDV_Player*);
 // Delete a player
void mdv_free_player (MDV_Player*);

void mdv_channel_set_drums (MDV_Player*, uint8_t channel, int is_drums);
int mdv_channel_is_drums (MDV_Player*, uint8_t channel);
void mdv_fast_forward_to_note (MDV_Player*);

///// Main sequences API /////

MDV_Sequence* mdv_load_midi (const char* filename);

void mdv_free_sequence (MDV_Sequence*);

void mdv_print_sequence (MDV_Sequence*);


///// Events API /////
// U = unimplemented
// I = ignored

enum MDV_Event_Type {
    MDV_NOTE_OFF = 0x08,
    MDV_NOTE_ON = 0x09,
    MDV_NOTE_AFTERTOUCH = 0x0A,  // U, should this change velocity?
    MDV_CONTROLLER = 0x0B,
    MDV_PROGRAM_CHANGE = 0x0C,
    MDV_CHANNEL_PRESSURE = 0x0D,  // U
    MDV_PITCH_BEND = 0x0E,
    MDV_COMMON = 0x0F,
     // Special custom event type, can't be sent over a real MIDI stream
    MDV_SET_TEMPO = 0x10
};
typedef uint8_t MDV_Event_Type;

enum MDV_Common_Event_Type {
    MDV_SYSEX = 0x0,  // U
    MDV_SONG_POSITION = 0x2,  // U
    MDV_SONG_SELECT = 0x3,  // U
    MDV_SYSEX_END = 0x7,  // U
    MDV_MIDI_CLOCK = 0x8,  // U
    MDV_MIDI_START = 0xA,  // U
    MDV_MIDI_CONTINUE = 0xB,  // U
    MDV_MIDI_STOP = 0xC,  // U
    MDV_RESET = 0xF,  // U
};
typedef uint8_t MDV_Common_Event_Type;

enum MDV_Controller {
    MDV_BANK_SELECT = 0, MDV_BANK_SELECT_MSB = 0,  // U
    MDV_DATA_ENTRY = 6, MDV_DATA_ENTRY_MSB = 6,  // U
    MDV_VOLUME = 7, MDV_VOLUME_MSB = 7,
    MDV_BALANCE = 8, MDV_BALANCE_MSV = 8,  // U
    MDV_PAN = 10, MDV_PAN_MSB = 10,
    MDV_EXPRESSION = 11, MDV_EXPRESSION_MSB = 11,
    MDV_BANK_SELECT_LSB = 32,  // I
    MDV_DATA_ENTRY_LSB = 38,  // U
    MDV_VOLUME_LSB = 39,  // I
    MDV_BALANCE_LSB = 40,  // I
    MDV_PAN_LSB = 42,  // I
    MDV_EXPRESSION_LSB = 43,  // I
    MDV_HOLD = 64,  // U
    MDV_SUSTENUTO = 66,  // U
    MDV_SOFT = 67,  // U
    MDV_LEGATO = 68,  // U
    MDV_HOLD_2 = 69,  // U
    MDV_RELEASE_TIME = 72,  // U
    MDV_ATTACK_TIME = 73,  // U
    MDV_REVERB = 91,  // U
    MDV_CHORUS = 93,  // U
    MDV_NRPN_LSB = 98,  // U
    MDV_NRPN_MSB = 99,  // U
    MDV_RPN_LSB = 100,  // U
    MDV_RPN_MSB = 101,  // U
    MDV_ALL_SOUND_OFF = 120,  // U
    MDV_ALL_CONTROLLERS_OFF = 121,  // U
    MDV_ALL_NOTES_OFF = 123,  // U
};
typedef uint8_t MDV_Controller;

enum MDV_RPN {
    MDV_PITCH_BEND_RANGE = 0x0000,  // U
    MDV_RPN_RESET = 0x3FFFF  // U
};
typedef uint16_t MDV_RPN;


static inline int mdv_parameters_used (uint8_t t) {
    if (t == MDV_PROGRAM_CHANGE || t == MDV_CHANNEL_PRESSURE)
        return 1;
    else
        return 2;
};

typedef struct MDV_Event {
    uint8_t type;
    uint8_t channel;
    uint8_t param1;
    uint8_t param2;
} MDV_Event;

void mdv_print_event (MDV_Event*);

typedef struct MDV_Timed_Event {
    uint32_t time;  // Absolute time in ticks
    MDV_Event event;
} MDV_Timed_Event;

typedef struct MDV_Sequence {
    uint32_t tpb;
    uint32_t n_events;
    MDV_Timed_Event* events;
} MDV_Sequence;


///// Patches API /////

typedef struct MDV_Sample {
     // in 16:16 Hz
    uint32_t low_freq;
    uint32_t high_freq;
    uint32_t root_freq;
     // 32:32 in samples
    int64_t loop_start;
    int64_t loop_end;
    uint32_t envelope_rates [6];
    uint32_t envelope_offsets [6];
     // 8:24 fixed point, I think
    int32_t tremolo_sweep_inc;
    int32_t tremolo_phase_inc;
    int32_t vibrato_sweep_inc;
    int32_t vibrato_phase_inc;
    int16_t tremolo_depth;
    int16_t vibrato_depth;

    uint8_t pan;
    uint8_t loop;
    uint8_t pingpong;
     // 32:32
    int64_t sample_inc;
    uint32_t data_size;
    int16_t* data;
} MDV_Sample;

typedef struct MDV_Patch {
     // TODO: do we need any more information?
    uint16_t volume;
    int8_t note;
    uint8_t n_samples;
    uint8_t keep_loop;
    uint8_t keep_envelope;
    MDV_Sample* samples;
} MDV_Patch;

MDV_Patch* _mdv_load_patch (const char* filename);
void mdv_free_patch (MDV_Patch*);

void mdv_print_patch (MDV_Patch*);

typedef struct MDV_Bank {
    MDV_Patch* patches [128];
    MDV_Patch* drums [128];
} MDV_Bank;

void mdv_bank_init (MDV_Bank*);
void mdv_bank_load_config (MDV_Bank*, const char* filename);
void mdv_bank_load_patch (MDV_Bank*, uint8_t instrument, const char* filename);
void mdv_bank_load_drum (MDV_Bank*, uint8_t instrument, const char* filename);
void mdv_bank_free_patches (MDV_Bank*);


///// Auxilary player API /////

 // Reset player to initial state
void mdv_reset_player (MDV_Player*);
 // Load an individual patch from a file
void mdv_load_patch (MDV_Player*, uint8_t index, const char* filename);
void mdv_load_drum (MDV_Player*, uint8_t drum, const char* filename);

#endif
