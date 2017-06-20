#include "libc.h"
#include <stdarg.h>
#include <stdio.h>

int sscanf(const char* restrict s, const char* restrict fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsscanf(s, fmt, ap);
    va_end(ap);
    return ret;
}
