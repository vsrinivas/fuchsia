#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void _panic(void* caller, const char* fmt, ...) {
    printf("panic (caller %p): ", caller);

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    abort();
}
