#include <stdlib.h>

#include <magenta/syscalls.h>

_Noreturn void abort(void) {
    // TODO(kulakowski) This can (and should) be more robust.
    for (;;) {
        mx_exit(-1);
    }
}
