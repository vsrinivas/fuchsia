// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

long long strtonum(const char *nptr, long long minval, long long maxval,
                   const char **errstr) {
  int len = strlen(nptr);
  char* endptr;
  long long val = strtoll(nptr, &endptr, 10);
  if ((endptr - nptr) != len || minval > maxval) {
    errno = EINVAL;
    if (errstr) {
      *errstr = "invalid";
    }
    return 0;
  }
  if ((errno == ERANGE && val == LLONG_MIN) || val < minval) {
    errno = ERANGE;
    if (errstr) {
      *errstr = "too small";
    }
    return 0;
  } else if ((errno == ERANGE && val == LLONG_MAX) || val > maxval) {
    errno = ERANGE;
    if (errstr) {
      *errstr = "too large";
    }
    return 0;
  }
  if (errstr) {
    *errstr = NULL;
  }
  return val;
}

void errc(int eval, int code, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  errno = code;
  verr(eval, fmt, ap);
  va_end(ap);
}

unsigned int arc4random(void) {
  static atomic_bool random_init = false;
  if (!random_init) {
    srandom(time(NULL));
    random_init = true;
  }
  return (unsigned int)random();
}
