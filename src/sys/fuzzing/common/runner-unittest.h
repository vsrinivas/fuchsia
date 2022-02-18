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
#include "src/sys/fuzzing/common/testing/module.h"

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
  void SetResult(const Input& input, FuzzResult result);
  void SetLeak(const Input& input, bool leak);

  const Coverage& GetCoverage(const Input& input);
  FuzzResult GetResult(const Input& input);
  bool HasLeak(const Input& input);

  // Fakes the interactions needed with the runner to perform a single fuzzing run.
  Input RunOne();

  // Like |RunOne()|, but the given parameters overrides any set by |SetResult|.
  Input RunOne(FuzzResult result);
  Input RunOne(const Coverage& coverage);
  Input RunOne(bool leak);

  // Fakes the interactions needed with the runner to perform a sequence of fuzzing runs until the
  // engine indicates it is idle. See also |HasStatus| below.
  void RunUntilIdle();

  // Waits until the runner is started and producing test inputs, or until it stops without
  // providing any inputs. Useful when another thread is responsible for driving the runner, e.g.
  // via |RunUntilIdle|.
  void AwaitStarted();

  // Returns false if the runner stops before providing any test inputs; otherwise waits for the
  // first input indefinitely and returns true. Unblocks |AwaitStarted| before returning.
  bool HasTestInput();

  // Like |HasTesInput|, except that it returns false if the given |deadline| expires before it
  // receives a test input.
  virtual bool HasTestInput(zx::time deadline) = 0;

  // Returns the test input for the next run. This must not be called unless |HasTestInput| returns
  // true.
  virtual Input GetTestInput() = 0;

  // Sts the feedback for the next run.
  virtual void SetFeedback(const Coverage& coverage, FuzzResult result, bool leak) = 0;

  // Returns whether |SetStatus| has been called. If true, the workflow is complete and the engine
  // is idle.
  bool HasStatus() const;

  // Blocks until a workflow completes and calls |SetStatus|, then returns its argument. Upon
  // return, the engine is idle.
  zx_status_t GetStatus();

  // Records the |status| of a fuzzing workflow. This implies the engine is now idle.
  void SetStatus(zx_status_t status);

  //////////////////////////////////////
  // Unit tests, organized by fuzzing workflow.

  void ExecuteNoError(Runner* runner);
  void ExecuteWithError(Runner* runner);
  void ExecuteWithLeak(Runner* runner);

  void MinimizeNoError(Runner* runner);
  void MinimizeEmpty(Runner* runner);
  void MinimizeOneByte(Runner* runner);
  void MinimizeReduceByTwo(Runner* runner);
  void MinimizeNewError(Runner* runner);

  void CleanseNoReplacement(Runner* runner);
  void CleanseAlreadyClean(Runner* runner);
  void CleanseTwoBytes(Runner* runner);

  void FuzzUntilError(Runner* runner);
  void FuzzUntilRuns(Runner* runner);
  void FuzzUntilTime(Runner* runner);

  // The |Merge| unit tests have extra parameters and are not included in runner-unittest.inc.
  // They should be added directly, e.g.:
  //
  //   TEST_F(DerivedRunnerTest, MergeSeedError) {
  //     DerivedRunner runner;
  //     MergeSeedError(&runner, /* expected= */ ZX_ERR_NOT_SUPPORTED);
  //   }

  // |expected| indicates the anticipated return value when merging a corpus with an error-causing
  // input.
  void MergeSeedError(Runner* runner, zx_status_t expected, uint64_t oom_limit = kDefaultOomLimit);

  // |keeps_errors| indicates whether merge keeps error-causing inputs in the final corpus.
  void Merge(Runner* runner, bool keeps_errors, uint64_t oom_limit = kDefaultOomLimit);

  void Stop(Runner* runner);

 private:
  struct Feedback {
    Coverage coverage;
    FuzzResult result = FuzzResult::NO_ERRORS;
    bool leak = false;
  };

  std::shared_ptr<Options> options_;
  std::unordered_map<std::string, Feedback> feedback_;
  SyncWait started_sync_;

  zx_status_t status_ = ZX_ERR_INTERNAL;
  SyncWait status_sync_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
