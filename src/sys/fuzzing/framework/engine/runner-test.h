// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_TEST_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_TEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <string>
#include <vector>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/runner-unittest.h"
#include "src/sys/fuzzing/common/testing/module.h"
#include "src/sys/fuzzing/framework/coverage/forwarder.h"
#include "src/sys/fuzzing/framework/engine/runner.h"
#include "src/sys/fuzzing/framework/testing/adapter.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

// Specializes the generic |RunnerTest| for |RunnerImpl|.
class RunnerImplTest : public RunnerTest {
 protected:
  void SetUp() override;

  const RunnerPtr& runner() const override { return runner_; }

  // FakeTargetAdapter methods.
  void SetAdapterParameters(const std::vector<std::string>& parameters);

  // RunnerTest methods.
  ZxPromise<Input> GetTestInput() override;
  ZxPromise<> SetFeedback(Coverage coverage, FuzzResult fuzz_result, bool leak) override;

 private:
  RunnerPtr runner_;
  std::unique_ptr<FakeTargetAdapter> target_adapter_;
  std::unique_ptr<FakeProcess> process_;
  std::unique_ptr<CoverageForwarder> coverage_forwarder_;
  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_TEST_H_
