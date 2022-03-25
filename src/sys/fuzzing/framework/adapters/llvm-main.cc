// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/92490): There ought to be an integration test under //src/sys/fuzzing/tests that
// fakes the channel from fuzz-test-runner when spawning a component fuzzer. Such a test would
// ensure this file is built and tested.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/framework/adapters/llvm.h"

int main(int argc, char const* argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto executor = MakeExecutor(loop.dispatcher());
  fuzzing::LLVMTargetAdapter adapter(executor());
  adapter.SetParameters(std::vector<std::string>(argv + 1, argv + argc));
  auto context = sys::ComponentContext::Create();
  auto outgoing = context->outgoing();
  outgoing->AddPublicService(adapter.GetHandler());
  outgoing->ServeFromStartupInfo(loop.dispatcher());
  executor->schedule_task(adapter.Run().then([](const Result<>& result) { exit(0); }));
  return loop.Run();
}
