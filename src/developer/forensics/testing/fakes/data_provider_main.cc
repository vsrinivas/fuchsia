// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/testing/fakes/data_provider.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"forensics", "test"});

  FX_LOGS(INFO) << "Starting FakeDataProvider";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  ::forensics::fakes::DataProvider data_provider;

  ::fidl::BindingSet<fuchsia::feedback::DataProvider> data_provider_bindings;
  context->outgoing()->AddPublicService(data_provider_bindings.GetHandler(&data_provider));

  loop.Run();

  return EXIT_SUCCESS;
}
