// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
#define SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>
#include <unordered_map>

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

  static std::shared_ptr<Options> DefaultOptions(Runner* runner);

  // Adds test-related |options| (e.g. PRNG seed) and configures the |runner|.
  virtual void Configure(Runner* runner, const std::shared_ptr<Options>& options);

  // Tests may set fake feedback to be "produced" during calls to |RunOne| with the given |input|.
  void SetCoverage(const Input& input, const Coverage& coverage);
  void SetResult(const Input& input, Result result);
  void SetLeak(const Input& input, bool leak);

  const Coverage& GetCoverage(const Input& input);
  Result GetResult(const Input& input);
  bool HasLeak(const Input& input);

  // Fakes the interactions needed with the runner to perform a single fuzzing run.
  Input RunOne();

  // Like |RunOne()|, but the given parameters overrides any set by |SetResult|.
  Input RunOne(Result result);
  Input RunOne(const Coverage& coverage);
  Input RunOne(bool leak);

  // Returns the test input for the next run.
  virtual Input GetTestInput() = 0;

  // Sts the feedback for the next run.
  virtual void SetFeedback(const Coverage& coverage, Result result, bool leak) = 0;

  // Records the |status| of a fuzzing workflow.
  void SetStatus(zx_status_t status);

  // Blocks until a workflow completes and calls |SetResult|, then returns its argument.
  zx_status_t GetStatus();

  //////////////////////////////////////
  // Unit tests, organized by fuzzing workflow.

  virtual void ExecuteNoError(Runner* runner);
  virtual void ExecuteWithError(Runner* runner);
  virtual void ExecuteWithLeak(Runner* runner);

  virtual void MinimizeNoError(Runner* runner);
  virtual void MinimizeEmpty(Runner* runner);
  virtual void MinimizeOneByte(Runner* runner);
  virtual void MinimizeReduceByTwo(Runner* runner);
  virtual void MinimizeNewError(Runner* runner);

  virtual void CleanseNoReplacement(Runner* runner);
  virtual void CleanseAlreadyClean(Runner* runner);
  virtual void CleanseTwoBytes(Runner* runner);

  virtual void FuzzUntilError(Runner* runner);
  virtual void FuzzUntilRuns(Runner* runner);
  virtual void FuzzUntilTime(Runner* runner);

  virtual void MergeSeedError(Runner* runner);
  virtual void Merge(Runner* runner);

  //////////////////////////////////////
  // Partial unit test implementations deferred to derived classes.

  // Provides runner-specific sequences of runs for individual unit tests.
  virtual void RunAllForCleanseTwoBytes() = 0;
  virtual void RunAllForFuzzUntilTime() = 0;
  virtual void RunAllForMerge() = 0;

  // Some engines (e.g. libFuzzer) discard error causing inputs when merging.
  virtual bool MergePreservesErrors() { return true; }

 private:
  struct Feedback {
    Coverage coverage;
    Result result = Result::NO_ERRORS;
    bool leak = false;
  };

  std::shared_ptr<Options> options_;
  std::unordered_map<std::string, Feedback> feedback_;
  zx_status_t status_ = ZX_ERR_INTERNAL;
  sync_completion_t sync_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
