#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#include "libc.h"

int vwscanf(const wchar_t* restrict fmt, va_list ap) { return vfwscanf(stdin, fmt, ap); }
