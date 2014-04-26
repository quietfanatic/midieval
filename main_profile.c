
#include <time.h>
#include <stdio.h>
#include "midi_files.h"
#include "player.h"

uint8_t dat [4096 * 4];

int main (int argc, char** argv) {
    MDV_Player* player = mdv_new_player();
    mdv_load_config(player, "/usr/local/share/eawpats/gravis.cfg");
    MDV_Sequence* seq = mdv_load_midi(argc == 2 ? argv[1] : "test.mid");
    mdv_play_sequence(player, seq);
    printf("dat: %p, player: %p, seq: %p\n", dat, player, seq);

    clock_t start = clock();
    while (mdv_currently_playing(player)) {
        mdv_get_audio(player, dat, 4096 * 4);
    }
    clock_t end = clock();
    printf("Time to render song: %f\n", (double)(end - start)/CLOCKS_PER_SEC);
    mdv_free_player(player);
    mdv_free_sequence(seq);
    return 0;
}
