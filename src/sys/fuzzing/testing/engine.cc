// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/testing/runner.h"

namespace fuzzing {

zx_status_t RunTestEngine() {
  Dispatcher dispatcher;
  auto executor = MakeExecutor(dispatcher.get());
  auto runner = SimpleFixedRunner::MakePtr(std::move(executor));
  ControllerProviderImpl provider;
  return provider.Run(std::move(runner));
}

}  // namespace fuzzing

int main() { return fuzzing::RunTestEngine(); }
