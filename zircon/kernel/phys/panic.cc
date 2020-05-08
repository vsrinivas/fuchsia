// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>

#include "frame-pointer.h"
#include "main.h"
#include "symbolize.h"

// This is what ZX_ASSERT calls.
PHYS_SINGLETHREAD void __zx_panic(const char* format, ...) {
  // Print the message.
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  // Now print the backtrace.
  printf("\nBacktrace:\n");
  Symbolize::GetInstance()->BackTrace(FramePointer::BackTrace());

  // Now crash.
  __builtin_trap();
}
