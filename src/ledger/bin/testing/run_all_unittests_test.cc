// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/run_all_unittests.h"

#include <lib/async-testing/test_loop.h>

#include <optional>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/base/log_severity.h"

namespace {

// These two variables should be set in main before the tests are run.
// The expected test loop seed, or nullopt if it should be random.
std::optional<int> expected_test_loop_seed;
// The expected log verbosity.
int expected_log_verbosity;

TEST(RunAllUnittestsTest, CheckTestLoopSeed) {
  async::TestLoop loop;
  if (expected_test_loop_seed) {
    // We expect a fixed seed.
    EXPECT_EQ(loop.initial_state(), *expected_test_loop_seed);
  } else {
    // We expect a random non-zero seed.
    EXPECT_NE(loop.initial_state(), 0);
  }
}

TEST(RunAllUnittestsTest, CheckLogVerbosity) {
  EXPECT_EQ(ledger::GetLogSeverity(), static_cast<absl::LogSeverity>(-expected_log_verbosity));
}

}  // namespace

// Tests that ledger::RunAllUnittests() parses the arguments as expected. This is not run in gtest
// because it needs to run gtest.
int main(int argc, char** argv) {
  // Run without arguments: the verbosity is 0 and the seed is random.
  std::vector<const char*> default_args = {"bin"};
  expected_test_loop_seed = std::nullopt;
  expected_log_verbosity = 0;
  LEDGER_LOG(INFO) << "Running tests without options";
  // const_cast: RunAllTests passes this into gtest. gtest may move/zero pointers in argv but does
  // not modify the individual arguments, so it is ok to use a |const char**| here.
  int returned =
      ledger::RunAllUnittests(default_args.size(), const_cast<char**>(default_args.data()));
  if (returned != 0) {
    return returned;
  }

  // Run with arguments.
  std::vector<const char*> full_args = {"bin", "--test_loop_seed=42", "--verbose=2"};
  expected_test_loop_seed = 42;
  expected_log_verbosity = 2;
  LEDGER_LOG(INFO) << "Running tests with options";
  return ledger::RunAllUnittests(full_args.size(), const_cast<char**>(full_args.data()));
}
