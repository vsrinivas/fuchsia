#include <sched.h>

#include <zircon/syscalls.h>

int sched_yield() {
    _zx_nanosleep(0ull);
    return 0;
}
