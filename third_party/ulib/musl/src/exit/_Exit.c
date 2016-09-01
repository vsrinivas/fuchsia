#include <stdlib.h>

#include <magenta/syscalls.h>

_Noreturn void _Exit(int ec) {
    for (;;) {
        _mx_exit(ec);
    }
}
