// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/substitute.h>
#include <lib/svc/cpp/services.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/pkg_url/fuchsia_pkg_url.h>
#include <trace-provider/provider.h>

static void Usage() {
  printf(
      "Usage: set_root_view url\n"
      "\n"
      "set_root_view displays a view in full-screen.  The view is connected "
      "to root_presenter and given its own Presentation.\n"
      "\n"
      "url should either be a full component URL, like:\n"
      "\"fuchsia-pkg://fuchsia.com/<package>#meta/<component>.cmx\"\n"
      "or the short name of a package (just <package>), in which case:\n"
      "\"fuchsia-pkg://fuchsia.com/<package>#meta/<package>.cmx\"\n"
      "will be launched.\n");
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  FXL_LOG(WARNING) << "NOTE: This tool is deprecated and WILL BE DELETED "
                   << "04/20/2019.  Use present_view instead.\n";

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("h") || command_line.HasOption("help")) {
    Usage();
    return 0;
  }

  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    Usage();
    return 1;
  }

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    Usage();
    return 1;
  }

  if (command_line.HasOption("input_path", nullptr)) {
    // Ease users off this flag.
    FXL_LOG(ERROR)
        << "The --input_path= flag is DEPRECATED. Flag will be removed.";
  }

  component::FuchsiaPkgUrl pkg_url;
  if (!pkg_url.Parse(positional_args[0])) {
    std::string converted =
        fxl::Substitute("fuchsia-pkg://fuchsia.com/$0#meta/$1.cmx",
                        positional_args[0], positional_args[0]);
    if (!pkg_url.Parse(converted)) {
      FXL_LOG(ERROR) << "Unable to launch " << positional_args[0] << ".  "
                     << "It is not a valid full package name or a valid "
                        "short package name.";
      Usage();
      return 1;
    }
  }

  auto startup_context_ = component::StartupContext::CreateFromStartupInfo();

  // Launch the component.
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = pkg_url.ToString();
  for (size_t i = 1; i < positional_args.size(); ++i)
    launch_info.arguments.push_back(positional_args[i]);
  launch_info.directory_request = services.NewRequest();
  fuchsia::sys::ComponentControllerPtr controller;
  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                controller.NewRequest());
  controller.set_error_handler([&loop](zx_status_t status) {
    FXL_LOG(INFO) << "Launched component terminated.";
    loop.Quit();
  });

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  // Create a View from the launched component.
  auto view_provider =
      services.ConnectToService<fuchsia::ui::app::ViewProvider>();
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  // Ask the presenter to display it.
  auto presenter =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>();
  presenter->PresentView(std::move(view_holder_token), nullptr);

  // Done!
  loop.Run();
  return 0;
}
