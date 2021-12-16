// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "test_settings.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    FX_LOGS(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

  // Setting this flag to true causes googletest to *generate* and log the random seed.
  GTEST_FLAG_SET(shuffle, true);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
