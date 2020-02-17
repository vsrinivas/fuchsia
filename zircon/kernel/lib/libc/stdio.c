// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <stdio.h>

extern int __printf_output_func(const char *str, size_t len, void *state);

int _printf(const char *fmt, ...) {
  va_list ap;
  int err;

  va_start(ap, fmt);
  err = _printf_engine(__printf_output_func, NULL, fmt, ap);
  va_end(ap);

  return err;
}

int _vprintf(const char *fmt, va_list ap) {
  return _printf_engine(__printf_output_func, NULL, fmt, ap);
}
