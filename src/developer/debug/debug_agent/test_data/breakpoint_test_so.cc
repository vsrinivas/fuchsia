// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

#include <atomic>

int InsertBreakpointFunction(int c) { return 10 * c; }

void AnotherFunctionForKicks() {}

void MultithreadedFunctionToBreakOn() {
  // This counter is meant to be a bare-bones example of multi-threaded logic.
  static std::atomic<int> global_counter = 0;
  global_counter++;
}
