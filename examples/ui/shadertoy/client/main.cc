// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/examples/ui/shadertoy/client/view.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/ui/scenic/cpp/view_provider_service.h"
#include "lib/ui/view_framework/view_provider_app.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto startup_context = component::StartupContext::CreateFromStartupInfo();

  // Export deprecated |fuchsia.ui.views_v1.ViewProvider| service.
  mozart::ViewProviderApp mozart_app(
      startup_context.get(), [](mozart::ViewContext view_context) {
        return std::make_unique<shadertoy_client::OldView>(
            view_context.startup_context, std::move(view_context.view_manager),
            std::move(view_context.view_owner_request));
      });

  auto scenic =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop.Quit();
  });

  // Export |fuchsia.ui.app.ViewProvider| service, so that this app can be
  // attached to the Scenic view tree.
  auto view_provider = std::make_unique<scenic::ViewProviderService>(
      startup_context.get(), scenic.get(), [](scenic::ViewFactoryArgs args) {
        return std::make_unique<shadertoy_client::NewView>(
            std::move(args), "Shadertoy Client Example");
      });

  loop.Run();
  return 0;
}
