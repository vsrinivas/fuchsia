// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

#include <stdio.h>
#include <string.h>

// This program is setup so that it needs to have registers written at key
// points so that it passes successfully.
//
// Scenarios:
//
// 1. RAX branch.
// 2. PC jump.

// Main ------------------------------------------------------------------------

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Wrong amount of arguments. Usage: <exe> <test\n");
    return 1;
  }

  // Check the test.
  const char* test = argv[1];
  if (strcmp(test, "branch_on_rax") == 0) {
    Test_BranchOnRAX();
  } else if (strcmp(test, "pc_jump") == 0) {
    Test_PCJump();
  } else {
    fprintf(stderr, "Unknown test: %s\n", test);
    return 1;
  }

  // Every test marks this boolean to see if it passed or not.
  return gTestPassed ? 0 : 1;
}
