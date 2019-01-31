#include "libc.h"
#include <stdarg.h>
#include <stdio.h>

int fscanf(FILE* restrict f, const char* restrict fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vfscanf(f, fmt, ap);
    va_end(ap);
    return ret;
}
