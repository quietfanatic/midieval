#include "player.h"

#include <stdlib.h>

struct Voice {
    Voice* prev;
    Voice* next;
    int16 phase;
    int16 wavelength;
    int16 volume;
};

struct Player {
    Voice* prev;
    Voice* next;
};

Player* new_player () {
    Player* player = (Player*)malloc(sizeof(Player));
    player->prev = (Voice*)player;
    player->next = (Voice*)player;
    return player;
}
void free_player (Player* player) {
    Voice* next_v;
    for (Voice* v = player->next; v != (Voice*)player; v = next_v) {
        next_v = v->next;
        free(v);
    }
    free(player);
}

Voice* add_voice (Player* player, uint16 wavelength, uint16 volume, uint16 phase) {
    Voice* v = (Voice*)malloc(sizeof(Voice));
    Voice* pvs = (Voice*)player;
    v->next = pvs;
    v->prev = pvs->prev;
    pvs->prev->next = v;
    pvs->prev = v;
    v->phase = phase;
    v->volume = volume;
    v->wavelength = wavelength;
    return v;
}
void remove_voice (Voice* v) {
    v->prev->next = v->prev;
    v->next->prev = v->next;
}

void get_audio (Player* player, uint8* buf_, int len) {
    int16* buf = (int16*)buf_;
    len /= 2;  // Assuming always an even number
    for (int i = 0; i < len; i++) {
        int32 val = 0;
        for (Voice* v = player->next; v != (Voice*)player; v = v->next) {
            val += v->phase < v->wavelength / 2 ? -v->volume : v->volume;
            v->phase += 1;
            v->phase %= v->wavelength;
        }
        buf[i] = val > 32767 ? 32767 : val < -32768 ? -32768 : val;
    }
}

