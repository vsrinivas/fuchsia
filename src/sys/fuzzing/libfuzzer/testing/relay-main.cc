// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/libfuzzer/testing/relay.h"

namespace fuzzing {

zx_status_t RunLibFuzzerRelay() {
  auto context = ComponentContext::Create();
  RelayImpl relay(context->executor());
  context->AddPublicService(relay.GetHandler());
  return context->Run();
}

}  // namespace fuzzing

int main(int argc, char** argv) { return fuzzing::RunLibFuzzerRelay(); }
