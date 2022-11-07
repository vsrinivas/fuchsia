// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fhcp/cpp/fhcp.h>
#include <stdarg.h>
#include <stdio.h>

namespace fhcp {

void PrintManualTestingMessage(const char* format, ...) {
  // ffx test will display messages written to stderr in the test console.
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}

}  // namespace fhcp
