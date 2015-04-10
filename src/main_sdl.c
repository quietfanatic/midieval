
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>

#include "midieval.h"

int main (int argc, char** argv) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
    }

     // Set up player
    MDV_Player* player = mdv_new_player();
    mdv_load_config(player, "/usr/local/share/eawpats/gravis.cfg");
    MDV_Sequence* seq = mdv_load_midi(argc == 2 ? argv[1] : "sample/test.mid");
    mdv_play_sequence(player, seq);
    mdv_fast_forward_to_note(player);

     // Set up SDL audio
    SDL_AudioSpec spec;
    spec.freq = MDV_SAMPLE_RATE;
    spec.format = AUDIO_S16;
    spec.channels = 2;
    spec.samples = 4096;
    spec.callback = (void(*)(void*,uint8_t*,int))mdv_get_audio;
    spec.userdata = player;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
    if (dev == 0) {
        printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    }

    SDL_PauseAudioDevice(dev, 0);
     // Interpret manually entered events in hex, for testing
    char buf [512];
    while (fgets(buf, 512, stdin)) {
        if (buf[0] == '\n')
            goto end;
        int len = 0;
        sscanf(buf, "%*[0123456789abcdefABCDEF]%n", &len);
        if (buf[len] != '\n')
            goto end;
        len = 0;
        for (char* t = buf; t[0] != '\n' && t[1] != '\n'; t += 2) {
            sscanf(t, "%2hhx", &buf[len]);
            len += 1;
        }
         // Super dumb event parsing.  TODO of course
        if (len == 0)
            goto end;
        printf("%02hhX %02hhX %02hhX\n", buf[0], buf[1], buf[2]);
        MDV_Event event;
        event.type = (uint8_t)buf[0] >> 4;
        event.channel = (uint8_t)buf[0] & 0x7;
        event.param1 = len >= 2 ? buf[1] : 0;
        event.param2 = len >= 3 ? buf[2] : 0;
        mdv_print_event(&event);
        SDL_LockAudioDevice(dev);
        mdv_play_event(player, &event);
        SDL_UnlockAudioDevice(dev);
    }
    end: { }
    SDL_PauseAudioDevice(dev, 1);

     // Clean up
    mdv_free_player(player);
    mdv_free_sequence(seq);
    SDL_Quit();
    return 0;
}
