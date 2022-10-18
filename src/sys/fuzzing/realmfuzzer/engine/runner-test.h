// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_RUNNER_TEST_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_RUNNER_TEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <memory>
#include <string>
#include <vector>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner-unittest.h"
#include "src/sys/fuzzing/common/testing/module.h"
#include "src/sys/fuzzing/realmfuzzer/engine/runner.h"
#include "src/sys/fuzzing/realmfuzzer/target/module.h"
#include "src/sys/fuzzing/realmfuzzer/testing/adapter.h"
#include "src/sys/fuzzing/realmfuzzer/testing/coverage.h"
#include "src/sys/fuzzing/realmfuzzer/testing/module.h"
#include "src/sys/fuzzing/realmfuzzer/testing/target.h"

namespace fuzzing {

// Specializes the generic |RunnerTest| for |RealmFuzzerRunner|. Encapsulates a fake target adapter,
// fake target process, and fake coverage component.
class RealmFuzzerRunnerTest : public RunnerTest {
 protected:
  void SetUp() override;

  const RunnerPtr& runner() const override { return runner_; }

  // FakeTargetAdapter methods.
  void SetAdapterParameters(const std::vector<std::string>& parameters);

  // RunnerTest methods.
  ZxPromise<Input> GetTestInput() override;
  ZxPromise<> SetFeedback(Coverage coverage, FuzzResult fuzz_result, bool leak) override;

  void TearDown() override;

 private:
  // Creates a fake target process and publishes it to the fake coverage component.
  ZxPromise<> PublishProcess();

  // Creates a fake LLVM module and publishes it to the fake coverage component.
  ZxPromise<> PublishModule();

  // Returns a promises that repeatedly waits for the engine to signal a fuzzing run is starting.
  // The process will automatically prepare its coverage and respond. The promise completes when the
  // process |Exit|s or |Crash|es.
  ZxPromise<> AwaitStart();

  // Returns a promises that waits for the engine to signal a fuzzing run is finishing. The process
  // will automatically update its coverage and respond.
  ZxPromise<> AwaitFinish();

  // Returns a promise to causes the spawned process to exit with the given |exitcode|.
  ZxPromise<> ExitAsync(int32_t exitcode);

  // Returns a promise to crash the spawned process and create an exception.
  ZxPromise<> CrashAsync();

  RunnerPtr runner_;
  std::unique_ptr<FakeTargetAdapter> target_adapter_;
  std::unique_ptr<FakeCoverage> coverage_;
  CoverageDataCollectorPtr collector_;
  std::unique_ptr<AsyncEventPair> eventpair_;
  FakeRealmFuzzerModule module_;
  std::unique_ptr<TestTarget> target_;
  bool leak_suspected_ = false;
  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_RUNNER_TEST_H_
