// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/processargs.h>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/testing/runner.h"

namespace fuzzing {

zx_status_t RunTestEngine() {
  // Take start up handles.
  auto context = ComponentContext::Create();
  zx::channel registry_channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};

  // Create the runner.
  auto runner = FakeRunner::MakePtr(context->executor());

  // Serve |fuchsia.fuzzer.ControllerProvider| to the registry.
  ControllerProviderImpl provider(context->executor());
  provider.SetRunner(std::move(runner));
  auto task = provider.Serve(std::move(registry_channel));
  context->ScheduleTask(std::move(task));

  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunTestEngine(); }
