#ifndef __REQUEST_H__
#include "segel.h"

typedef struct {
    struct timeval arrival_time;
    struct timeval interval;
    int thread_id;
    int thread_count;
    int *static_count;
    int *dynamic_count;
} stats_t;

void requestHandle(int fd, stats_t stats);

#endif
