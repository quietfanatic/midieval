#ifndef MIDIVAL_PLAYER_H
#define MIDIVAL_PLAYER_H

#include "events.h"
#include "patches.h"

typedef struct Player Player;

Player* new_player ();
void reset_player (Player*);
void free_player (Player*);

void play_sequence (Player*, Sequence*);
void set_patch (Player*, Patch*);

void get_audio (Player*, uint8* buf, int len);

#endif
