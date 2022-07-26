// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/engine.h"
#include "src/sys/fuzzing/realmfuzzer/engine/runner.h"

namespace fuzzing {

ZxResult<RunnerPtr> MakeRealmFuzzerRunnerPtr(int argc, char** argv, ComponentContext& context) {
  auto runner = RealmFuzzerRunner::MakePtr(context.executor());
  auto runner_impl = std::static_pointer_cast<RealmFuzzerRunner>(runner);
  runner_impl->SetTargetAdapterHandler(context.MakeRequestHandler<TargetAdapter>());
  if (auto status = runner_impl->BindCoverageDataProvider(context.TakeChannel(1));
      status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to bind fuchsia.fuzzer.CoverageDataProvider: "
                   << zx_status_get_string(status);
  }
  return fpromise::ok(std::move(runner));
}

}  // namespace fuzzing

int main(int argc, char** argv) {
  return fuzzing::RunEngine(argc, argv, fuzzing::MakeRealmFuzzerRunnerPtr);
}
