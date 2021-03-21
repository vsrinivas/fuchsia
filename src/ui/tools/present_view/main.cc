// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <zircon/status.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/ui/tools/present_view/present_view.h"

namespace {

constexpr char kKeyLocale[] = "locale";

present_view::ViewInfo ParseCommandLine(const fxl::CommandLine& command_line) {
  present_view::ViewInfo view_info;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    return {};
  }

  view_info.url = positional_args[0];
  for (size_t i = 1; i < positional_args.size(); ++i) {
    view_info.arguments.push_back(positional_args[i]);
  }
  if (command_line.HasOption(kKeyLocale)) {
    command_line.GetOptionValue(kKeyLocale, &view_info.locale);
  }

  return view_info;
}

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    FX_LOGS(INFO) << "Unable to set log settings from command line.";
    return 1;
  }

  bool present_view_error = false;
  present_view::ViewInfo view_info = ParseCommandLine(command_line);
  present_view::PresentView present_view(
      sys::ComponentContext::CreateAndServeOutgoingDirectory(),
      [&loop, &present_view_error](std::string error_string, zx_status_t status) {
        FX_LOGS(INFO) << error_string << "; status: " << zx_status_get_string(status);
        present_view_error = true;
        loop.Quit();
      });

  if (!present_view.Present(std::move(view_info))) {
    FX_LOGS(INFO) << "present_view requires the url of an application to display.";
    return 1;
  }
  loop.Run();

  return present_view_error ? 1 : 0;
}
