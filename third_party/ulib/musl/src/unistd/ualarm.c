#define _GNU_SOURCE
#include <sys/time.h>
#include <unistd.h>

unsigned ualarm(unsigned value, unsigned interval) {
    struct itimerval it = {.it_interval.tv_usec = interval, .it_value.tv_usec = value};
    setitimer(ITIMER_REAL, &it, &it);
    return it.it_value.tv_sec * 1000000 + it.it_value.tv_usec;
}
