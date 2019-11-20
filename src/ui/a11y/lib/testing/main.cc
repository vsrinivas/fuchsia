// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"

// TODO(40886): Use a common facility when it exists.
int main(int argc, char** argv) {
  syslog::InitLogger();

  if (!fxl::SetTestSettings(argc, argv)) {
    FX_LOGS(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
