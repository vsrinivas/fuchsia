#include <threads.h>

#include <magenta/syscalls.h>

void thrd_yield() {
    _mx_nanosleep(0ull);
}
