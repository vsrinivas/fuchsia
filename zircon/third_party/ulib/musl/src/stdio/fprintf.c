#include <stdarg.h>
#include <stdio.h>

int fprintf(FILE* restrict f, const char* restrict fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}
