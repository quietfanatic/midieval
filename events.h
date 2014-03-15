#ifndef MIDIVAL_EVENTS_H
#define MIDIVAL_EVENTS_H

#include <inttypes.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

enum Event_Type {
    NOTE_OFF = 0x08,
    NOTE_ON = 0x09,
    NOTE_AFTERTOUCH = 0x0A,
    CONTROLLER = 0x0B,
    PROGRAM_CHANGE = 0x0C,
    CHANNEL_AFTERTOUCH = 0x0D,
    PITCH_BEND = 0x0E,
    META = 0x0F,
    SET_TEMPO = 0x10
};
enum Controller {
    BANK_SELECT = 1,
    MODULATION = 2,
    DATA_ENTRY_MSB = 6,
    VOLUME = 7,
    BALANCE = 8,
    PAN = 10,
    EXPRESSION = 11,
    DATA_ENTRY_LSB = 38,
    NRPN_LSB = 98,
    NRPN_MSB = 99,
    RPN_LSB = 100,
    RPN_MSB = 101
};

static inline int parameters_used (uint8 t) {
    if (t == PROGRAM_CHANGE || t == CHANNEL_AFTERTOUCH)
        return 1;
    else
        return 2;
};

typedef struct Event {
    uint8 type;
    uint8 channel;
    uint8 param1;
    uint8 param2;
} Event;

typedef struct Timed_Event {
    uint32 time;  // In ticks
    Event event;
} Timed_Event;

typedef struct Sequence {
    uint32 tpb;
    uint32 n_events;
    Timed_Event* events;
} Sequence;

void free_sequence (Sequence*);

void print_event (Event*);
void print_sequence (Sequence*);

#endif
