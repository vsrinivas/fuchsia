// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/testing/runner.h"

namespace fuzzing {

zx_status_t RunTestEngine() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto executor = MakeExecutor(loop.dispatcher());
  ControllerProviderImpl provider(executor);
  auto runner = SimpleFixedRunner::MakePtr(executor);
  executor->schedule_task(provider.Run(std::move(runner)));
  return loop.Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunTestEngine(); }
