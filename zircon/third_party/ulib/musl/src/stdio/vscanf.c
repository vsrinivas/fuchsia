#include <stdarg.h>
#include <stdio.h>

#include "libc.h"

int vscanf(const char* restrict fmt, va_list ap) { return vfscanf(stdin, fmt, ap); }
