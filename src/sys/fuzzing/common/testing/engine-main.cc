// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/engine.h"
#include "src/sys/fuzzing/common/testing/runner.h"

namespace fuzzing {

ZxResult<RunnerPtr> MakeFakeRunnerPtr(int argc, char** argv, ComponentContext& context) {
  return fpromise::ok(FakeRunner::MakePtr(context.executor()));
}

}  // namespace fuzzing

int main(int argc, char** argv) {
  return fuzzing::RunEngine(argc, argv, fuzzing::MakeFakeRunnerPtr);
}
