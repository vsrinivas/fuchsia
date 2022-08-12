// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zircon/assert.h>

#include <cstdarg>
extern "C" void pw_assert_basic_HandleFailure(const char* file_name, int line_number,
                                              const char* function_name, const char* format, ...) {
  va_list args;
  va_start(args, format);
  ZX_PANIC(format, args);
  va_end(args);
}
