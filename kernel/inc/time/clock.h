#ifndef KERNEL_TIME_TIME_H
#define KERNEL_TIME_TIME_H

#include "common/types.h"
#include "common/list.h"

#define MILLIS_PER_SEC 1000
#define FEMPTOS_PER_SEC 1000000000000000ULL

typedef struct clock {
    list_head_t list;

    char *name;
    uint32_t rating;
    uint32_t freq;

    uint64_t (*read)(void);
} clock_t;

typedef struct clock_event_source clock_event_source_t;

struct clock_event_source {
    list_head_t list;

    char *name;
    uint32_t rating;
    //milliseconds_per_second
    uint32_t freq;

    void (*event)(clock_event_source_t *);
};

typedef struct clock_event_listener {
    list_head_t list;

    void (*handle)(clock_event_source_t *);
} clock_event_listener_t;

void register_clock(clock_t *clock);
void register_clock_event_source(clock_event_source_t *clock_event_source);
void register_clock_event_listener(clock_event_listener_t *clock_event_listener);

uint64_t uptime(); //In miliseconds
void sleep(uint32_t millis);

#endif
