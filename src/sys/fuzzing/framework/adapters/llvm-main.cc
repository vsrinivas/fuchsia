// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include "src/sys/fuzzing/framework/adapters/llvm.h"
#include "src/sys/fuzzing/framework/common/dispatcher.h"

int main(int argc, char const *argv[]) {
  auto dispatcher = std::make_shared<Dispatcher>();
  fuzzing::LLVMTargetAdapter adapter(dispatcher);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(
      adapter.GetHandler(/* on_close= */ [&dispatcher]() { dispatcher.Quit(); }));
  return dispatcher.Join();
}
