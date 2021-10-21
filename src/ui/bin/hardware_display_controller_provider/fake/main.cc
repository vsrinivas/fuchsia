// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/ui/bin/hardware_display_controller_provider/fake/service.h"

int main(int argc, const char** argv) {
  bool use_vsync2 = false;
  for (const auto& arg : cpp20::span(argv + 1, argc - 1)) {
    if (strcmp(arg, "--use-vsync2") == 0) {
      use_vsync2 = true;
    }
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> app_context(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());

  FX_LOGS(INFO) << "Starting fake fuchsia.hardware.display.Provider service.";

  fake_display::ProviderService hdcp_service_impl(app_context.get(), loop.dispatcher(), use_vsync2);

  loop.Run();

  FX_LOGS(INFO) << "Quit fake HardwareDisplayControllerProvider main loop.";

  return 0;
}
