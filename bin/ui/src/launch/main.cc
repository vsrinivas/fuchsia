// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/services/launcher/launcher.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FTL_LOG(ERROR) << "Launch requires the url of a view provider application "
                      "to launch.";
    return 1;
  }

  mtl::MessageLoop loop;
  auto application_context_ =
      modular::ApplicationContext::CreateFromStartupInfo();

  // Launch application.
  modular::ServiceProviderPtr services;
  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = positional_args[0];
  for (size_t i = 1; i < positional_args.size(); ++i)
    launch_info->arguments.push_back(positional_args[i]);
  launch_info->services = GetProxy(&services);
  FTL_LOG(INFO) << "Launching view provider " << launch_info->url;
  application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                      nullptr);

  // Create the view.
  fidl::InterfacePtr<mozart::ViewProvider> view_provider;
  modular::ConnectToService(services.get(), GetProxy(&view_provider));
  fidl::InterfaceHandle<mozart::ViewOwner> view_owner;
  view_provider->CreateView(fidl::GetProxy(&view_owner), nullptr);

  // Ask the launcher to display it.
  auto launcher =
      application_context_->ConnectToEnvironmentService<mozart::Launcher>();
  launcher->Display(std::move(view_owner));

  // Done!
  loop.PostQuitTask();
  loop.Run();
  return 0;
}
