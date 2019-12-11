// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"

ABSL_FLAG(int, test_loop_seed, 0, "random seed for the test loop");
ABSL_FLAG(int, verbose, 0, "level of verbosity");

namespace ledger {

int RunAllUnittests(int argc, char** argv) {
  // This consumes gtest-related arguments.
  testing::InitGoogleTest(&argc, argv);

  absl::ParseCommandLine(argc, argv);
  SetLogVerbosity(absl::GetFlag(FLAGS_verbose));

  if (absl::GetFlag(FLAGS_test_loop_seed) != 0) {
    setenv("TEST_LOOP_RANDOM_SEED", std::to_string(absl::GetFlag(FLAGS_test_loop_seed)).c_str(),
           /*overwrite=*/true);
  }

  return RUN_ALL_TESTS();
}

}  // namespace ledger
