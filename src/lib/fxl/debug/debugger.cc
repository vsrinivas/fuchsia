// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/lib/fxl/debug/debugger.h"

namespace fxl {

void BreakDebugger() {
  // On some other systems, Crashpad (the crash catcher and reporter) doesn't treat debug exceptions
  // as uploadable. On Fuchsia it does. Using a debug breakpoint here instead of __builtin_trap()
  // is a little more friendly to debuggers since they can transparently continue past the debug
  // breakpoint if the debugger user wants to continue running.
  __builtin_debugtrap();
}

}  // namespace fxl
