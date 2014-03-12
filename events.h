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
    META = 0x0F
};
enum Meta_Type {
    SET_TEMPO = 0x51
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

typedef struct Channel_Event {
    uint8 type;  // One of 0x08..0x0E
    uint8 channel;
    uint8 param1;  // Note number, controller number
    uint8 param2;  // Note velocity, controller value
} Channel_Event;

typedef struct Meta_Event {
    uint8 type;  // 0xFF
    uint8 meta_type;
    uint32 data_size;  // TODO: structure size optimization
    uint8* data;
} Meta_Event;

typedef union Event {
    uint8 type;
    Channel_Event channel_event;
    Meta_Event meta_event;
} Event;

typedef struct Timed_Event {
    uint32 time;  // In ticks
    Event event;
} Timed_Event;

void print_event (Event*);

#endif
