// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "src/lib/debug/debug.h"

// Manual test:
//   - ffx component run fuchsia-pkg://fuchsia.com/zxdb_e2e_inferiors#meta/wait_for_debugger.cm
//   - ffx debug connect
//   - [zxdb] attach wait_for_debugger.cm
//   - The process should stop on a breakpoint.
//   - [zxdb] continue
//   - The process should exit.
int main() {
  debug::WaitForDebugger();
  printf("Hello, Debugger!");
}
