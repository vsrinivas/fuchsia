// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FXL_LOG(ERROR) << "screencap requires a path for where to save "
                   << "the screenshot.";
    return 1;
  }
  const std::string filename = positional_args[0];

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());
  std::unique_ptr<component::ApplicationContext> app_context(
      component::ApplicationContext::CreateFromStartupInfo());
  // Connect to the SceneManager service.
  auto scenic =
      app_context->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(ERROR) << "Lost connection to Scenic service.";
    loop.Quit();
  });
  auto done_cb = [&loop](bool status) {
    if (!status) {
      FXL_LOG(ERROR) << "TakeScreenshot failed";
    }
    loop.Quit();
  };
  scenic->TakeScreenshot(filename, done_cb);
  loop.Run();

  return 0;
}
