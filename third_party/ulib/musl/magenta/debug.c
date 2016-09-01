#include <stdarg.h>
#include <stdio.h>

#include <magenta/syscalls.h>

void _panic(void* caller, const char* fmt, ...) {
    printf("panic (caller %p): ", caller);

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    // call the exit syscall in a loop to satisfy NO_RETURN compiler semantics
    for (;;) {
        _mx_exit(1);
    }
}
