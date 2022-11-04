// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DEBUG_DEBUG_H_
#define SRC_LIB_DEBUG_DEBUG_H_

namespace debug {

// Return whether there's a debugger attached to the current process.
bool IsDebuggerAttached();

// Wait until a debugger attaches, and then issue a breakpoint. The debugger can
// continue the execution.
//
// If there's no debugger attached within |seconds| seconds, the breakpoint will
// still be issued and the process will crash.
void WaitForDebugger(int seconds = 60);

}  // namespace debug

#endif  // SRC_LIB_DEBUG_DEBUG_H_
