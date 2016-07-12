#include <stdlib.h>

#include <magenta/syscalls.h>

_Noreturn void _Exit(int ec) {
    for (;;) {
        mx_exit(ec);
    }
}
