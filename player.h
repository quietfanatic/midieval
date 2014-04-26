#ifndef MIDIVAL_PLAYER_H
#define MIDIVAL_PLAYER_H

#include "events.h"

 // TODO: publicize this for serializability and such
typedef struct MDV_Player MDV_Player;

 // Core API

 // Allocate new player
MDV_Player* mdv_new_player ();
 // Load a .cfg containing patch names (nothing complicated please)
void mdv_load_config (MDV_Player*, const char* filename);
 // Set the sequence currently being played (use load_midi)
void mdv_play_sequence (MDV_Player*, MDV_Sequence*);
 // Get this many bytes of audio.  len must be a multiple of 4
void mdv_get_audio (MDV_Player*, uint8_t* buf, int len);
 // 0 if either no sequence was given or the sequence is done
int mdv_currently_playing (MDV_Player*);
 // Delete a player
void mdv_free_player (MDV_Player*);


 // Things you probably won't need

 // Reset player to initial state
void mdV_reset_player (MDV_Player*);
 // Load an individual patch from a file
void mdv_load_patch (MDV_Player*, uint8_t index, const char* filename);
void mdv_load_drum (MDV_Player*, uint8_t drum, const char* filename);
#endif
