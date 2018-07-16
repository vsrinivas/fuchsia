// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_LOG(WARNING) << "BE ADVISED: The set_root_view tool takes the URL to an "
                      "app that provided the ViewProvider interface and makes "
                      "it's view the root view.";
  FXL_LOG(WARNING)
      << "This tool is intended for testing and debugging purposes "
         "only and may cause problems if invoked incorrectly.";
  FXL_LOG(WARNING)
      << "Do not invoke set_root_view if a view tree already exists "
         "(i.e. if any process that creates a view is already "
         "running).";
  FXL_LOG(WARNING)
      << "If scenic is already running on your system you "
         "will probably want to kill it before invoking this tool.";

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FXL_LOG(ERROR)
        << "set_root_view requires the url of a view provider application "
           "to set_root_view.";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto startup_context_ = component::StartupContext::CreateFromStartupInfo();

  // Launch application.
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = positional_args[0];
  for (size_t i = 1; i < positional_args.size(); ++i)
    launch_info.arguments.push_back(positional_args[i]);
  launch_info.directory_request = services.NewRequest();
  fuchsia::sys::ComponentControllerPtr controller;
  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                controller.NewRequest());
  controller.set_error_handler([&loop] {
    FXL_LOG(INFO) << "Launched application terminated.";
    loop.Quit();
  });

  // Create the view.
  fidl::InterfacePtr<::fuchsia::ui::views_v1::ViewProvider> view_provider;
  services.ConnectToService(view_provider.NewRequest());
  fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner> view_owner;
  view_provider->CreateView(view_owner.NewRequest(), nullptr);

  // Ask the presenter to display it.
  auto presenter =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>();
  presenter->Present(std::move(view_owner), nullptr);

  // Done!
  loop.Run();
  return 0;
}
