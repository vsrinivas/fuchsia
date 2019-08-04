#include <stdarg.h>
#include <stdio.h>

#include "libc.h"

int scanf(const char* restrict fmt, ...) {
  int ret;
  va_list ap;
  va_start(ap, fmt);
  ret = vscanf(fmt, ap);
  va_end(ap);
  return ret;
}
