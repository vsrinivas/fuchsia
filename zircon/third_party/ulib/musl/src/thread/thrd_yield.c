#include <threads.h>
#include <zircon/syscalls.h>

void thrd_yield(void) { _zx_thread_legacy_yield(0ul); }
