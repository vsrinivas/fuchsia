// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/testing/coverage.h"

namespace fuzzing {
zx_status_t RunCoverage() {
  // Take start up handles.
  auto context = ComponentContext::Create();

  // Create the coverage forwarder.
  FakeCoverage coverage(context->executor());

  // See the note in target/instrumented-process.cc.
  // Serve `fuchsia.fuzzer.CoverageDataCollector` as `fuchsia.debugdata.Publisher`.
  if (auto status = context->AddPublicService(coverage.GetPublisherHandler()); status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve fuchsia.debugdata.Publisher: "
                   << zx_status_get_string(status);
    return status;
  }
  // Serve `fuchsia.fuzzer.CoverageDataProvider`.
  if (auto status = context->AddPublicService(coverage.GetProviderHandler()); status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve fuchsia.fuzzer.CoverageDataProvider: "
                   << zx_status_get_string(status);
    return status;
  }

  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunCoverage(); }
