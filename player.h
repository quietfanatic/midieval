#ifndef MIDIVAL_PLAYER_H
#define MIDIVAL_PLAYER_H

#include "events.h"

typedef struct Player Player;

 // Core API

 // Allocate new player
Player* new_player ();
 // Load a .cfg containing patch names (nothing complicated please)
void load_config (Player*, const char* filename);
 // Set the sequence currently being played (use load_midi)
void play_sequence (Player*, Sequence*);
 // Get this many bytes of audio.  len must be a multiple of 4
void get_audio (Player*, uint8* buf, int len);
 // 0 if either no sequence was given or the sequence is done
int currently_playing (Player*);
 // Delete a player
void free_player (Player*);


 // Things you probably won't need

 // Reset player to initial state
void reset_player (Player*);
 // Load an individual patch from a file
void load_patch (Player*, uint8 index, const char* filename);
void load_drum (Player*, uint8 drum, const char* filename);
#endif
