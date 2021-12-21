// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/adapters/llvm.h"

int main(int argc, char const *argv[]) {
  fuzzing::LLVMTargetAdapter adapter;
  adapter.SetParameters(std::vector<std::string>(argv + 1, argv + argc));
  auto context = sys::ComponentContext::Create();
  auto outgoing = context->outgoing();
  outgoing->AddPublicService(adapter.GetHandler());
  outgoing->ServeFromStartupInfo(adapter.dispatcher());
  fuzzing::DisableSlowWaitLogging();
  return adapter.Run();
}
