// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(__Fuchsia__)
#include <lib/syslog/cpp/logger.h>
#endif

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "test_settings.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    FXL_LOG(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

#if defined(__Fuchsia__)
  char* name = strrchr(argv[0], '/');
  std::string pname = "";
  if (name == nullptr) {
    pname = std::string(argv[0]);
  } else {
    pname = std::string(name + 1);
  }
  syslog::InitLogger({std::move(pname)});
#endif

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
