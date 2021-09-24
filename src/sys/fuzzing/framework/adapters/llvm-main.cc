// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/framework/adapters/llvm.h"

int main(int argc, char const *argv[]) {
  fuzzing::LLVMTargetAdapter adapter;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(adapter.GetHandler([&loop]() { loop.Quit(); }));
  return loop.Run();
}
