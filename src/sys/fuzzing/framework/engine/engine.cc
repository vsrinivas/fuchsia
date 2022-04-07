// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/framework/engine/runner.h"

namespace fuzzing {

zx_status_t RunFrameworkEngine() {
  auto context = ComponentContext::Create();
  ControllerProviderImpl provider(context->executor());
  auto runner = RunnerImpl::MakePtr(context->executor());
  auto runner_impl = std::static_pointer_cast<RunnerImpl>(runner);
  runner_impl->set_target_adapter_handler(context->MakeRequestHandler<TargetAdapter>());
  runner_impl->set_coverage_provider_handler(context->MakeRequestHandler<CoverageProvider>());
  auto task = provider.Run(std::move(runner));
  context->ScheduleTask(std::move(task));
  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunFrameworkEngine(); }
