// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "examples/media/audio/vu_meter/vu_meter_view.h"
#include "lib/ui/base_view/cpp/view_provider_component.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  scenic::ViewProviderComponent component(
      [&loop](scenic::ViewContext view_context) {
        return std::make_unique<examples::VuMeterView>(std::move(view_context),
                                                       &loop);
      },
      &loop);

  loop.Run();
  return 0;
}
