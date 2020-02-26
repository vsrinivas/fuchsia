// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/developer/feedback/testing/fakes/fake_data_provider.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback", "test"});

  FX_LOGS(INFO) << "Starting FakeDataProvider";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  feedback::FakeDataProvider data_provider;

  fidl::BindingSet<fuchsia::feedback::DataProvider> data_provider_bindings;
  context->outgoing()->AddPublicService(data_provider_bindings.GetHandler(&data_provider));

  loop.Run();

  return EXIT_SUCCESS;
}
