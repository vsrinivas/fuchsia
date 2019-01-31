#include <stdlib.h>

#include <zircon/syscalls.h>

_Noreturn void abort(void) {
    for (;;) {
        __builtin_trap();
        _zx_process_exit(-1);
    }
}
