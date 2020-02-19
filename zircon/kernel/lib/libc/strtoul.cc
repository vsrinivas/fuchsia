// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ctype.h>
#include <stdlib.h>

unsigned long strtoul(const char *nptr, char **endptr, int base) {
  int neg = 0;
  unsigned long ret = 0;

  if (base < 0 || base == 1 || base > 36) {
    return 0;
  }

  while (isspace(*nptr)) {
    nptr++;
  }

  if (*nptr == '+') {
    nptr++;
  } else if (*nptr == '-') {
    neg = 1;
    nptr++;
  }

  if ((base == 0 || base == 16) && nptr[0] == '0' && nptr[1] == 'x') {
    base = 16;
    nptr += 2;
  } else if (base == 0 && nptr[0] == '0') {
    base = 8;
    nptr++;
  } else if (base == 0) {
    base = 10;
  }

  for (;;) {
    char c = *nptr;
    int v = -1;
    unsigned long new_ret;

    if (c >= 'A' && c <= 'Z') {
      v = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'z') {
      v = c - 'a' + 10;
    } else if (c >= '0' && c <= '9') {
      v = c - '0';
    }

    if (v < 0 || v >= base) {
      if (endptr) {
        *endptr = (char *)nptr;
      }
      break;
    }

    new_ret = ret * base;
    if (new_ret / base != ret || new_ret + v < new_ret || ret == ULONG_MAX) {
      ret = ULONG_MAX;
    } else {
      ret = new_ret + v;
    }

    nptr++;
  }

  if (neg && ret != ULONG_MAX) {
    ret = -ret;
  }

  return ret;
}
