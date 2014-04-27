#include "midival.h"

#include <stdio.h>
#include <stdlib.h>

void mdv_free_sequence (MDV_Sequence* seq) {
    free(seq->events);
    free(seq);
}

void mdv_print_event (MDV_Event* event) {
    printf("%02hhX %02hhX %02hhX %02hhX\n",
        event->type, event->channel, event->param1, event->param2
    );
}
void mdv_print_sequence (MDV_Sequence* seq) {
    for (uint32_t i = 0; i < seq->n_events; i++) {
        printf("%08x ", seq->events[i].time);
        mdv_print_event(&seq->events[i].event);
    }
}
