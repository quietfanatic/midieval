#include "events.h"

#include <stdio.h>

void print_event (Event* event) {
    if (event->type == 0xff) {
        Meta_Event* me = &event->meta_event;
        printf("FF %02hhX ", me->meta_type);
        for (size_t j = 0; j < me->data_size; j++) {
            printf("%02hhX", me->data[j]);
        }
        printf("\n");
    }
    else {
        Channel_Event* ce = &event->channel_event;
        if (parameters_used(ce->type) == 2) {
            printf("%01hhX %01hhX %02hhX %02hhX\n",
                ce->type, ce->channel, ce->param1, ce->param2
            );
        }
        else {
            printf("%01hhX%01hhX %02hhX\n",
                ce->type, ce->channel, ce->param1
            );
        }
    }
}
