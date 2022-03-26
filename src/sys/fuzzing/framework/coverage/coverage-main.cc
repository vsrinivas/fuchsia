// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/coverage/forwarder.h"

namespace fuzzing {

zx_status_t RunCoverageForwarder() {
  ComponentContext context;
  CoverageForwarder forwarder(context.executor());
  context.AddPublicService(forwarder.GetInstrumentationHandler());
  context.AddPublicService(forwarder.GetCoverageProviderHandler());
  return context.Run();
}

}  // namespace fuzzing

int main(int argc, char const *argv[]) { return fuzzing::RunCoverageForwarder(); }
