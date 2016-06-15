#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>

int asprintf(char** s, const char* fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vasprintf(s, fmt, ap);
    va_end(ap);
    return ret;
}
