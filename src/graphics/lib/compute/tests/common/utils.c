// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void
assert_panic_(const char * file, int line, const char * fmt, ...)
{
  va_list args;
  fprintf(stderr, "ERROR:%s:%d:", file, line);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  abort();
}
