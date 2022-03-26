// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/adapters/llvm.h"

namespace fuzzing {

zx_status_t RunLLVMTargetAdapter(const std::vector<std::string>& args) {
  ComponentContext context;
  LLVMTargetAdapter adapter(context.executor());
  adapter.SetParameters(args);
  context.AddPublicService(adapter.GetHandler());
  return context.Run();
}

}  // namespace fuzzing

int main(int argc, char const* argv[]) {
  return fuzzing::RunLLVMTargetAdapter(std::vector<std::string>(argv + 1, argv + argc));
}
