#include <stdlib.h>

#include <magenta/syscalls.h>

_Noreturn void abort(void) {
    for (;;) {
        __builtin_trap();
        mx_exit(-1);
    }
}
