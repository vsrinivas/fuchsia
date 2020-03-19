// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "test_settings.h"

#ifdef __Fuchsia__
#include "src/lib/syslog/cpp/logger.h"
#endif

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    FXL_LOG(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }
#ifdef __Fuchsia__
  syslog::InitLogger();
#endif
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
