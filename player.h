#ifndef MIDIVAL_PLAYER_H
#define MIDIVAL_PLAYER_H

#include "events.h"

typedef struct Voice Voice;
typedef struct Player Player;

Player* new_player ();
void free_player (Player*);

Voice* add_voice (Player*, uint16 wavelength, uint16 volume, uint16 phase);
void remove_voice (Voice*);

void get_audio (Player*, uint8* buf, int len);

#endif
