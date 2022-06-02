// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/coverage/forwarder.h"

namespace fuzzing {

zx_status_t RunCoverageForwarder() {
  // Take start up handles.
  auto context = ComponentContext::Create();

  // Serve |fuchsia.fuzzer.Instrumentation| and |fuchsia.fuzzer.CoverageProvider|.
  CoverageForwarder forwarder(context->executor());
  if (auto status = context->AddPublicService(forwarder.GetInstrumentationHandler());
      status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve fuchsia.fuzzer.Instrumentation: "
                   << zx_status_get_string(status);
    return status;
  }
  if (auto status = context->AddPublicService(forwarder.GetCoverageProviderHandler());
      status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve fuchsia.fuzzer.CoverageProvider: "
                   << zx_status_get_string(status);
    return status;
  }

  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunCoverageForwarder(); }
