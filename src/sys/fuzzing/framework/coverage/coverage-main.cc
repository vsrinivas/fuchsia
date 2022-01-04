// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/forwarder.h"

int main(int argc, char const *argv[]) {
  CoverageForwarder forwarder;
  auto context = sys::ComponentContext::Create();
  auto outgoing = context->outgoing();
  outgoing->AddPublicService(forwarder.GetInstrumentationHandler());
  outgoing->AddPublicService(forwarder.GetCoverageProviderHandler());
  outgoing->ServeFromStartupInfo(forwarder.dispatcher());
  forwarder.Run();
  return ZX_OK;
}
