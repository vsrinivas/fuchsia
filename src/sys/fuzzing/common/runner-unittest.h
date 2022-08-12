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

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
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
class RunnerTest : public AsyncTest {
 protected:
  //////////////////////////////////////
  // Test fixtures.
  void SetUp() override;

  virtual const RunnerPtr& runner() const = 0;

  const OptionsPtr& options() { return options_; }

  // Adds test-related |options| (e.g. PRNG seed) and configures the |runner|.
  virtual void Configure(const OptionsPtr& options);

  // Tests may set fake coverage to be "produced" during calls to |RunOne| with the given |input|.
  void SetCoverage(const Input& input, Coverage coverage);

  // Tests may provide a |handler| that determines the fuzz result for a given |input|.
  using FuzzResultHandler = fit::function<FuzzResult(const Input&)>;
  void SetFuzzResultHandler(FuzzResultHandler handler);

  // Tests may indicate if all inputs will simulate leaking memory.
  void SetLeak(bool has_leak);

  // These methods correspond to those above, but with a given |hex| string representing the input.
  // Additionally, the |hex| strings may contain "x" as a wildcard. A hex string from an input
  // matches a wildcarded string if they are the same length and every character matches or is "x".
  // In the cases of multiple matches, the longest non-wildcarded prefix wins.
  void SetCoverage(const std::string& hex, Coverage coverage);
  void SetFuzzResult(const std::string& hex, FuzzResult fuzz_result);
  void SetLeak(const std::string& hex, bool has_leak);

  // Fakes the interactions needed with the runner to perform a single fuzzing run.
  Promise<Input> RunOne();

  // Like |RunOne()|, but the given parameters overrides any values set by the corresponding
  // |Set...| methods.
  Promise<Input> RunOne(FuzzResult result);
  Promise<Input> RunOne(Coverage coverage);
  Promise<Input> RunOne(bool leak);

  // Fakes the interactions needed with the runner to perform a sequence of fuzzing runs until the
  // given |promise| completes.
  void RunUntil(Promise<> promise);

  // Like |RunUntil|, but additionally takes a |RunCallback| that can choose how to invoke |RunOne|
  // based on the previous result. The initial result is constructed as |fpromise::ok(input)|.
  using RunCallback = fit::function<Promise<Input>(const Result<Input>&)>;
  void RunUntil(Promise<> promise, RunCallback run, Input input);

  // Returns the test input for the next run. This must not be called unless |HasTestInput| returns
  // true.
  virtual ZxPromise<Input> GetTestInput() = 0;

  // Sets the feedback for the next run.
  virtual ZxPromise<> SetFeedback(Coverage coverage, FuzzResult result, bool leak) = 0;

  //////////////////////////////////////
  // Unit tests, organized by fuzzing workflow.

  void ExecuteNoError();
  void ExecuteWithError();
  void ExecuteWithLeak();

  void MinimizeNoError();
  void MinimizeEmpty();
  void MinimizeOneByte();
  void MinimizeReduceByTwo();
  void MinimizeNewError();

  void CleanseNoReplacement();
  void CleanseAlreadyClean();
  void CleanseTwoBytes();

  void FuzzUntilError();
  void FuzzUntilRuns();
  void FuzzUntilTime();

  // The |Merge| unit tests have extra parameters and are not included in runner-unittest.inc.
  // They should be added directly, e.g.:
  //
  //   TEST_F(DerivedRunnerTest, MergeSeedError) {
  //     DerivedRunner runner;
  //     MergeSeedError(&runner, /* expected= */ ZX_ERR_NOT_SUPPORTED);
  //   }

  // |expected| indicates the anticipated return value when merging a corpus with an error-causing
  // input.
  void MergeSeedError(zx_status_t expected, uint64_t oom_limit = kDefaultOomLimit);

  // |keeps_errors| indicates whether merge keeps error-causing inputs in the final corpus.
  void Merge(bool keeps_errors, uint64_t oom_limit = kDefaultOomLimit);

  void Stop();

 private:
  // Like |RunOne| above, but allows callers to provide a function to |set_feedback| based on the
  // current |Input|.
  Promise<Input> RunOne(fit::function<ZxPromise<>(const Input&)> set_feedback);

  OptionsPtr options_;
  std::unordered_map<std::string, Coverage> coverage_;
  FuzzResultHandler handler_;
  bool has_leak_ = false;

  // Calls to |RunOne| use this bridge consumer to ensure they run sequentially and in order.
  fpromise::consumer<> previous_run_;

  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUNNER_UNITTEST_H_
