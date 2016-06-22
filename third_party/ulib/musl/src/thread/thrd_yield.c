#include <threads.h>

#include <magenta/syscalls.h>

void thrd_yield() {
    _magenta_nanosleep(0ull);
}
