// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
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
    return 1;
  }

  FXL_LOG(WARNING) << "BE ADVISED: The present_view tool takes the URL to an "
                      "app that provided the ViewProvider interface and makes "
                      "its view the root view.";
  FXL_LOG(WARNING) << "This tool is intended for testing and debugging purposes "
                      "only and may cause problems if invoked incorrectly.";
  FXL_LOG(WARNING) << "Do not invoke present_view if a view tree already exists "
                      "(i.e. if any process that creates a view is already "
                      "running).";
  FXL_LOG(WARNING) << "If scenic is already running on your system you "
                      "will probably want to kill it before invoking this tool.";

  present_view::ViewInfo view_info = ParseCommandLine(command_line);
  present_view::PresentView present_view(sys::ComponentContext::Create());

  int retval = 0;
  bool present_success =
      present_view.Present(std::move(view_info), [&loop, &retval](zx_status_t status) {
        FXL_LOG(INFO) << "Launched component terminated; status: " << status;
        retval = 1;
        loop.Quit();
      });
  if (!present_success) {
    return 1;
  }

  loop.Run();

  return retval;
}
