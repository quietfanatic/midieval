#ifndef MIDIVAL_PLAYER_H
#define MIDIVAL_PLAYER_H

#include "events.h"

typedef struct Player Player;

Player* new_player ();
void free_player (Player*);

void play_sequence (Player*, Sequence*);

void get_audio (Player*, uint8* buf, int len);

#endif
