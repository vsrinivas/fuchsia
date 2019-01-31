#include "libc.h"
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

int wscanf(const wchar_t* restrict fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vwscanf(fmt, ap);
    va_end(ap);
    return ret;
}
