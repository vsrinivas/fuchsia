// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>

#include <phys/frame-pointer.h>
#include <phys/main.h>
#include <phys/stack.h>
#include <phys/symbolize.h>

// This is what ZX_ASSERT calls.
PHYS_SINGLETHREAD void __zx_panic(const char* format, ...) {
  // Print the message.
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  // The format string typically won't have a \n at the end (and is not
  // required to have \n at the end to avoid a run-on line).  Any format
  // strings that have \n at the end will result in an extra empty line, which
  // can be avoided (if desired) by removing the \n from the end of the format
  // string.
  //
  // The semantics of ZX_PANIC() is that a \n at the end of the format string
  // is not needed to avoid a run-on line, so we ensure we won't have a run-on
  // line here.
  printf("\n");

  // Now print the backtrace and stack dump.
  if (gSymbolize) {
    gSymbolize->PrintBacktraces(FramePointer::BackTrace(),
                                gSymbolize->GetShadowCallStackBacktrace());

    uintptr_t sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    gSymbolize->PrintStack(sp);
  }

  // Now crash.
  ArchPanicReset();
}
