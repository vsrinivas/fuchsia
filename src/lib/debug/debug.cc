// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/debug/debug.h"

#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace debug {

bool IsDebuggerAttached() {
  zx_handle_t self = zx_process_self();
  // ZX_PROP_PROCESS_BREAK_ON_LOAD is only set when a debugger attaches. A better approach is to ask
  // the kernel to provide us a |debugger_attached| field in |zx_info_process_t|.
  uint64_t break_on_load = 0;
  zx_object_get_property(self, ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load,
                         sizeof(break_on_load));
  return break_on_load;
}

void WaitForDebugger(int seconds) {
  while (seconds-- && !IsDebuggerAttached()) {
    sleep(1);
  }
  // After finishing all setup, type "continue" in the debugger to continue.
  __builtin_debugtrap();
}

}  // namespace debug
