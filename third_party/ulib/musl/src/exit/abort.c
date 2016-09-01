#include <stdlib.h>

#include <magenta/syscalls.h>

_Noreturn void abort(void) {
    for (;;) {
        __builtin_trap();
        _mx_exit(-1);
    }
}
