// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note this file may be used for multiple guest integration test binaries. Do not add any logic
// to this file that is specific to any one test binary or suite.

#include <gtest/gtest.h>

#include "logger.h"
#include "src/lib/fxl/test/test_settings.h"

// This test event listener dumps the guest's serial logs when a test fails.
class LoggerOutputListener : public ::testing::EmptyTestEventListener {
  void OnTestEnd(const ::testing::TestInfo& info) override {
    if (!info.result()->Failed()) {
      return;
    }
    std::cout << "[----------] Begin guest output\n";
    std::cout << Logger::Get().Buffer();
    std::cout << "\n[----------] End guest output\n";
    std::cout.flush();
  }
};

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  // Switch to line buffering of stdout/stderr, so that we don't lose
  // log lines if a test hangs.
  //
  // TODO(fxbug.dev/10218): Solve this globally for everyone, not just for this
  // test suite.
  std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
  std::setvbuf(stderr, nullptr, _IOLBF, BUFSIZ);

  LoggerOutputListener listener;

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  return status;
}
