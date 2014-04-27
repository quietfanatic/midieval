
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

#include "midival.h"

int main (int argc, char** argv) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
    }

     // Set up player
    MDV_Player* player = mdv_new_player();
    mdv_load_config(player, "/usr/local/share/eawpats/gravis.cfg");
    MDV_Sequence* seq = mdv_load_midi(argc == 2 ? argv[1] : "test.mid");
    mdv_play_sequence(player, seq);

     // Set up SDL audio
    SDL_AudioSpec spec;
    spec.freq = 48000;
    spec.format = AUDIO_S16;
    spec.channels = 2;
    spec.samples = 4096;
    spec.callback = (void(*)(void*,uint8_t*,int))mdv_get_audio;
    spec.userdata = player;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    if (dev == 0) {
        printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    }

     // Play until input
    SDL_PauseAudioDevice(dev, 0);
    fgetc(stdin);
    SDL_PauseAudioDevice(dev, 1);

     // Clean up
    mdv_free_player(player);
    mdv_free_sequence(seq);
    SDL_Quit();
    return 0;
}
