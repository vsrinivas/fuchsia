// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/processargs.h>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/framework/engine/runner.h"

namespace fuzzing {

zx_status_t RunFrameworkEngine() {
  // Take start up handles.
  auto context = ComponentContext::Create();
  zx::channel fuzz_registry_channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  zx::channel fuzz_coverage_channel{zx_take_startup_handle(PA_HND(PA_USER0, 1))};

  // Create the runner.
  auto runner = RunnerImpl::MakePtr(context->executor());
  auto runner_impl = std::static_pointer_cast<RunnerImpl>(runner);
  runner_impl->SetTargetAdapterHandler(context->MakeRequestHandler<TargetAdapter>());
  if (auto status = runner_impl->BindCoverageDataProvider(std::move(fuzz_coverage_channel));
      status != ZX_OK) {
    return status;
  }

  // Serve |fuchsia.fuzzer.ControllerProvider| to the registry.
  ControllerProviderImpl provider(context->executor());
  provider.SetRunner(std::move(runner));
  auto task = provider.Serve(std::move(fuzz_registry_channel));
  context->ScheduleTask(std::move(task));

  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunFrameworkEngine(); }
