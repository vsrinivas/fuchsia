// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

int main() {
  // This function will touch a global variable the debugger will set a
  // watchpoint on.
  WatchpointFunction();
}
