// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "src/sys/fuzzing/common/engine.h"
#include "src/sys/fuzzing/libfuzzer/runner.h"

namespace fuzzing {

ZxResult<RunnerPtr> MakeLibFuzzerRunnerPtr(int argc, char** argv, ComponentContext& context) {
  auto runner = LibFuzzerRunner::MakePtr(context.executor());
  auto runner_impl = std::static_pointer_cast<LibFuzzerRunner>(runner);
  argv = &argv[1];
  argc -= 1;
  std::vector<std::string> cmdline(argv, argv + argc);
  runner_impl->set_cmdline(cmdline);
  return fpromise::ok(std::move(runner));
}

}  // namespace fuzzing

int main(int argc, char** argv) {
  return fuzzing::RunEngine(argc, argv, fuzzing::MakeLibFuzzerRunnerPtr);
}
