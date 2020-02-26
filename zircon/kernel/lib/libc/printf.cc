// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <stdio.h>

#undef printf
#undef vprintf

int vprintf(const char* fmt, va_list args) { return vfprintf(stdout, fmt, args); }

int printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = vprintf(fmt, args);
  va_end(args);
  return result;
}
