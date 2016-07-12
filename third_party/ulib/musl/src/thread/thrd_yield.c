#include <threads.h>

#include <magenta/syscalls.h>

void thrd_yield() {
    mx_nanosleep(0ull);
}
