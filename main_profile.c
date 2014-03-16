
#include <time.h>
#include <stdio.h>
#include "midi_files.h"
#include "player.h"

uint8 dat [4096 * 4];

int main (int argc, char** argv) {
    Player* player = new_player();
    load_config(player, "/usr/local/share/eawpats/gravis.cfg");
    Sequence* seq = load_midi(argc == 2 ? argv[1] : "test.mid");
    play_sequence(player, seq);

    clock_t start = clock();
    while (currently_playing(player)) {
        get_audio(player, dat, 4096 * 4);
    }
    clock_t end = clock();
    printf("Time to render song: %f\n", (double)(end - start)/CLOCKS_PER_SEC);
    free_player(player);
    free_sequence(seq);
    return 0;
}
