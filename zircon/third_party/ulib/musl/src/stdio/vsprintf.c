#include <limits.h>
#include <stdio.h>

int vsprintf(char* restrict s, const char* restrict fmt, va_list ap) {
    return vsnprintf(s, INT_MAX, fmt, ap);
}
