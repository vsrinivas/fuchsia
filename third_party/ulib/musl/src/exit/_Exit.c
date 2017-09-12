#include <stdlib.h>

#include <zircon/syscalls.h>

_Noreturn void _Exit(int ec) {
    for (;;) {
        _zx_process_exit(ec);
    }
}
