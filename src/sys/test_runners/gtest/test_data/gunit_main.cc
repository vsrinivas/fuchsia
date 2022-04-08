// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fxl/test/test_settings.h"

// As we don't have a way to write gunit tests in-tree, we will simulate them by
// replacing gunit flags with gtest flags.

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    FX_LOGS(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

  const std::string gunit_flag_start = "--gunit_";
  const std::string gtest_flag = "--gtest_";

  for (int i = 0; i < argc; i++) {
    if (strncmp(gunit_flag_start.c_str(), argv[i], gunit_flag_start.length()) == 0) {
      strncpy(argv[i], gtest_flag.c_str(), gtest_flag.length());
    } else if (strncmp(gtest_flag.c_str(), argv[i], gtest_flag.length()) == 0) {
      FX_LOGS(ERROR) << "got gtest flag in gunit simulated test: " << argv[i];
      exit(1);
    }
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
