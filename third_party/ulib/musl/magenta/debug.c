#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void _warn_unsupported(void* caller, const char* fmt, ...) {
    fprintf(stderr, "warning (caller %p): ", caller);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
