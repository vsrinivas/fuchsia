// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

int main(int argc, char* argv[]) {
  int res = InsertBreakpointFunction(argc - 1);
  return res;
}
