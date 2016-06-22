#include <sched.h>

#include <magenta/syscalls.h>

int sched_yield() {
    _magenta_nanosleep(0ull);
    return 0;
}
