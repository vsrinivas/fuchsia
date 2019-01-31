#include <threads.h>

#include <zircon/syscalls.h>

void thrd_yield() {
    _zx_nanosleep(0ull);
}
