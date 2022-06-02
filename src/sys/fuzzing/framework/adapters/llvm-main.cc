// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/adapters/llvm.h"

namespace fuzzing {

zx_status_t RunLLVMTargetAdapter(const std::vector<std::string>& args) {
  // Take start up handles.
  auto context = ComponentContext::Create();

  // Create the fuzz target adapter.
  LLVMTargetAdapter adapter(context->executor());
  adapter.SetParameters(args);

  // Serve |fuchsia.fuzzer.TargetAdapter|.
  if (auto status = context->AddPublicService(adapter.GetHandler()); status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve fuchsia.fuzzer.TargetAdapter: "
                   << zx_status_get_string(status);
    return status;
  }

  return context->Run();
}

}  // namespace fuzzing

int main(int argc, char const* argv[]) {
  return fuzzing::RunLLVMTargetAdapter(std::vector<std::string>(argv + 1, argv + argc));
}
