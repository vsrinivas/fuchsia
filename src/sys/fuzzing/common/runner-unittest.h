// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
#define SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/testing/coverage.h"

namespace fuzzing {

// Just as |Runner| is the base class for specific runner implementations, this class contains
// generic runner unit tests that can be used as the basis for the specific implementations' unit
// tests.
//
// To use these tests for, e.g. a "DerivedRunner" class and a "DerivedRunnerTest" test fixture,
// include code like the following:
//
//   #define RUNNER_TYPE DerivedRunner
//   #define RUNNER_TEST DerivedRunnerTest
//   #include "src/sys/fuzzing/controller/runner-unittest.inc"
//   #undef RUNNER_TEST
//   #undef RUNNER_TYPE
//
class RunnerTest : public ::testing::Test {
 protected:
  //////////////////////////////////////
  // Test fixtures.

  const std::shared_ptr<Options>& options() { return options_; }
  bool leak_suspected() const { return leak_suspected_; }

  void set_leak_suspected(bool leak_suspected) { leak_suspected_ = leak_suspected; }

  // Adds test-related |options| (e.g. PRNG seed) and configures the |runner|.
  virtual void Configure(Runner* runner, const std::shared_ptr<Options>& options);

  // Records the |result| of a fuzzing workflow.
  void SetResult(zx_status_t result);

  // Blocks until a workflow completes and calls |SetResult|, then returns its argument.
  zx_status_t GetResult();

  // Tests may set fake code |coverage| to be "produced" during a subsequent call to |RunOne|.
  virtual void SetCoverage(const Coverage& coverage) = 0;

  // Fakes the interactions needed with the runner to perform a single fuzzing run. Tests may
  // indicate if the run should encounter an error or complete normally (using |Result::NO_ERRORS|).
  // In the latter case, tests may also indicate whether the run should appear to have more
  // |malloc|s than |free|s.
  virtual Input RunOne(Result expected) = 0;

  //////////////////////////////////////
  // Unit tests, organized by fuzzing workflow.

  void ExecuteNoError(Runner* runner);
  void ExecuteWithError(Runner* runner);
  void ExecuteWithLeak(Runner* runner);

  void MinimizeNoError(Runner* runner);
  void MinimizeOneByte(Runner* runner);
  void MinimizeReduceByTwo(Runner* runner);
  void MinimizeNewError(Runner* runner);

  void CleanseNoError(Runner* runner);
  void CleanseNoReplacement(Runner* runner);
  void CleanseAlreadyClean(Runner* runner);
  void CleanseTwoBytes(Runner* runner);

  void FuzzUntilError(Runner* runner);
  void FuzzUntilRuns(Runner* runner);
  void FuzzUntilTime(Runner* runner);

  void MergeSeedError(Runner* runner);
  void Merge(Runner* runner);

 private:
  std::shared_ptr<Options> options_;
  zx_status_t result_ = ZX_ERR_INTERNAL;
  sync_completion_t sync_;
  bool leak_suspected_ = false;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
