#include "events.h"

#include <stdio.h>
#include <stdlib.h>

void free_sequence (Sequence* seq) {
    free(seq->events);
    free(seq);
}

void print_event (Event* event) {
    printf("%02hhX %02hhX %02hhX %02hhX\n",
        event->type, event->channel, event->param1, event->param2
    );
}
void print_sequence (Sequence* seq) {
    for (uint32 i = 0; i < seq->n_events; i++) {
        printf("%08x ", seq->events[i].time);
        print_event(&seq->events[i].event);
    }
}
