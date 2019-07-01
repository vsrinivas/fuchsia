// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

// This simply binary is used for the breakpoint integration test.
// It calls some functions that the debugger will attempt to insert breakpoints in.

int main(int argc, char* argv[]) {
  int res = InsertBreakpointFunction(argc - 1);
  res = InsertBreakpointFunction2(res);
  return res;
}
