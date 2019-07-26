// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <unittest/unittest.h>

#include "inferior.h"
#include "utils.h"

namespace {

void scan_argv(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "v=", 2) == 0) {
      int verbosity = atoi(argv[i] + 2);
      unittest_set_verbosity_level(verbosity);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  g_program_path = argv[0];
  scan_argv(argc, argv);

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
  }

  bool success = unittest_run_all_tests(argc, argv);
  return success ? 0 : -1;
}
