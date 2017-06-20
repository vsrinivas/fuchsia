#include "libc.h"
#include <stdarg.h>
#include <stdio.h>

int scanf(const char* restrict fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vscanf(fmt, ap);
    va_end(ap);
    return ret;
}
