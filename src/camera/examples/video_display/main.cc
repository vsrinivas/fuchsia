// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include "src/camera/examples/video_display/simple_camera_view.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/ui/base_view/view_provider_component.h"

/*
  To run this code, log in and then run the following command:
  > fx shell sessionctl add_mod
  fuchsia-pkg://fuchsia.com/video_display#meta/video_display.cmx
*/

int main(int argc, const char** argv) {
  syslog::InitLogger({"video_display"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  scenic::ViewProviderComponent component(
      [](scenic::ViewContext view_context) {
        return std::make_unique<video_display::SimpleCameraView>(std::move(view_context));
      },
      &loop);

  loop.Run();
  return 0;
}
