#include "events.h"

#include <stdio.h>

void print_event (Event* event) {
    printf("%02hhX %02hhX %02hhX %02hhX\n",
        event->type, event->channel, event->param1, event->param2
    );
}
