// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/testing/runner.h"

namespace fuzzing {

zx_status_t RunTestEngine() {
  ComponentContext context;
  ControllerProviderImpl provider(context.executor());
  auto runner = SimpleFixedRunner::MakePtr(context.executor());
  auto task = provider.Run(std::move(runner));
  context.ScheduleTask(std::move(task));
  return context.Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunTestEngine(); }
