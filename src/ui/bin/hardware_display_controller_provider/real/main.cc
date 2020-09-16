// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <memory>

#include "src/ui/lib/display/hardware_display_controller_provider_impl.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> app_context(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());

  FX_LOGS(INFO) << "Starting standalone fuchsia.hardware.display.Provider service.";

  ui_display::HardwareDisplayControllerProviderImpl hdcp_service_impl(app_context.get());

  loop.Run();

  FX_LOGS(INFO) << "Quit HardwareDisplayControllerProvider main loop.";

  return 0;
}
