#include "libc.h"
#include <stdarg.h>
#include <stdio.h>

int vscanf(const char* restrict fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

weak_alias(vscanf, __isoc99_vscanf);
