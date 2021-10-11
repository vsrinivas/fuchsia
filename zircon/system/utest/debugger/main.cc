// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <zxtest/zxtest.h>

#include "inferior.h"
#include "utils.h"

int main(int argc, char** argv) {
  g_program_path = argv[0];

  if (argc >= 2) {
    if (strcmp(argv[1], kTestInferiorChildName) == 0) {
      return test_inferior();
    }

    if (strcmp(argv[1], kTestSegfaultChildName) == 0) {
      return test_segfault();
    }

    if (strcmp(argv[1], kTestSwbreakChildName) == 0) {
      return test_sw_break();
    }

    if (strcmp(argv[1], kTestSuspendOnStart) == 0) {
      return test_suspend_on_start();
    }

    if (strcmp(argv[1], kTestDynBreakOnLoad) == 0) {
      return test_dyn_break_on_load();
    }
  }

  return zxtest::RunAllTests(argc, argv);
}
