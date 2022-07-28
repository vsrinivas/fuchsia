// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ENGINE_H_
#define SRC_SYS_FUZZING_COMMON_ENGINE_H_

#include <string>
#include <vector>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

// Starts the engine with runner provided by |MakeRunnerPtr|, which should have the signature:
// `ZxResult<RunnerPtr>(int, char**, ComponentContext&)`. This should be called from `main`, and
// the first two parameters should be |argc| and |argv|, respectively.
template <typename RunnerPtrMaker>
zx_status_t RunEngine(int argc, char** argv, RunnerPtrMaker MakeRunnerPtr) {
  auto context = ComponentContext::Create();
  ControllerProviderImpl provider(context->executor());

  // Extract command line arguments for the controller provider.
  if (auto status = provider.Initialize(&argc, &argv); status != ZX_OK) {
    return status;
  }

  // Create the runner.
  auto result = MakeRunnerPtr(argc, argv, *context);
  if (result.is_error()) {
    return result.error();
  }
  provider.SetRunner(result.take_value());

  // Serve |fuchsia.fuzzer.ControllerProvider| to the registry.
  auto task = provider.Serve(context->TakeChannel(0));
  context->ScheduleTask(std::move(task));

  return context->Run();
}

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ENGINE_H_
