// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/assert.h>
#include <zircon/compiler.h>

extern "C" __NO_RETURN __PRINTFLIKE(1, 2) void __zx_panic(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fflush(NULL);
  abort();
}
