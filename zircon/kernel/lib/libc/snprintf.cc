// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <stdio.h>
#include <string-file.h>

#include <ktl/algorithm.h>
#include <ktl/move.h>

int vsnprintf(char* buf, size_t len, const char* fmt, va_list args) {
  StringFile out({buf, len});
  int ret = vfprintf(&out, fmt, args);
  ktl::move(out).take();
  return ret;
}

int snprintf(char* buf, size_t len, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = vsnprintf(buf, len, fmt, args);
  va_end(args);
  return result;
}
