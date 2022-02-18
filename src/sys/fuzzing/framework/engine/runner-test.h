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
  // RunnerTest methods.
  void Configure(Runner* runner, const std::shared_ptr<Options>& options) override;
  bool HasTestInput(zx::time deadline) override;
  Input GetTestInput() override;
  void SetFeedback(const Coverage& coverage, FuzzResult result, bool leak) override;

  // FakeTargetAdapter methods.
  void SetAdapterParameters(const std::vector<std::string>& parameters);

 private:
  FakeTargetAdapter target_adapter_;
  FakeProcess process_;
  CoverageForwarder coverage_forwarder_;
  bool stopped_ = true;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_TEST_H_
