// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/libfuzzer/testing/relay.h"

namespace fuzzing {

zx_status_t RunLibFuzzerRelay() {
  // Take start up handles.
  auto context = ComponentContext::Create();

  // Create the test relay.
  RelayImpl relay(context->executor());

  // Serve |test.fuzzer.Relay|.
  if (auto status = context->AddPublicService(relay.GetHandler()); status != ZX_OK) {
    FX_LOGS(ERROR) << " Failed to serve test.fuzzer.Relay: " << zx_status_get_string(status);
    return status;
  }

  return context->Run();
}

}  // namespace fuzzing

int main() { return fuzzing::RunLibFuzzerRelay(); }
