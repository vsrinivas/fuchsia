#define _GNU_SOURCE
#include <sys/time.h>
#include <time.h>

int stime(const time_t* t) {
    struct timeval tv = {.tv_sec = *t, .tv_usec = 0};
    return settimeofday(&tv, (void*)0);
}
