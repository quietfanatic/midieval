#ifndef MIDIVAL_EVENTS_H
#define MIDIVAL_EVENTS_H

#include <inttypes.h>

enum MDV_Event_Type {
    MDV_NOTE_OFF = 0x08,
    MDV_NOTE_ON = 0x09,
    MDV_NOTE_AFTERTOUCH = 0x0A,
    MDV_CONTROLLER = 0x0B,
    MDV_PROGRAM_CHANGE = 0x0C,
    MDV_CHANNEL_AFTERTOUCH = 0x0D,
    MDV_PITCH_BEND = 0x0E,
    MDV_META = 0x0F,
    MDV_SET_TEMPO = 0x10
};
typedef uint8_t MDV_Event_Type;
enum MDV_Controller {
    MDV_BANK_SELECT = 1,
    MDV_MODULATION = 2,
    MDV_DATA_ENTRY_MSB = 6,
    MDV_VOLUME = 7,
    MDV_BALANCE = 8,
    MDV_PAN = 10,
    MDV_EXPRESSION = 11,
    MDV_DATA_ENTRY_LSB = 38,
    MDV_NRPN_LSB = 98,
    MDV_NRPN_MSB = 99,
    MDV_RPN_LSB = 100,
    MDV_RPN_MSB = 101
};
typedef uint8_t MDV_Controller;

static inline int mdv_parameters_used (uint8_t t) {
    if (t == MDV_PROGRAM_CHANGE || t == MDV_CHANNEL_AFTERTOUCH)
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

typedef struct MDV_Timed_Event {
    uint32_t time;  // Absolute time in ticks
    MDV_Event event;
} MDV_Timed_Event;

typedef struct MDV_Sequence {
    uint32_t tpb;
    uint32_t n_events;
    MDV_Timed_Event* events;
} MDV_Sequence;

void mdv_free_sequence (MDV_Sequence*);

void mdv_print_event (MDV_Event*);
void mdv_print_sequence (MDV_Sequence*);

#endif
