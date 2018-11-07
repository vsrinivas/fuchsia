// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/examples/ui/shadertoy/client/view.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/ui/base_view/cpp/view_provider_component.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const bool use_old_view = command_line.HasOption("use_old_view");

  // Export |fuchsia.ui.viewsv1.ViewProvider| or |fuchsia.ui.app.ViewProvider|
  // service, so that this component can be attached to the Scenic scene graph.
  if (!use_old_view) {
    scenic::ViewProviderComponent component(
        [](scenic::ViewContext context) {
          return std::make_unique<shadertoy_client::NewView>(
              std::move(context), "Shadertoy Client Example (V2View)");
        },
        &loop);

    loop.Run();
  } else {
    scenic::ViewProviderComponent component(
        [](scenic::ViewContext context) {
          return std::make_unique<shadertoy_client::OldView>(
              std::move(context), "Shadertoy Client Example (V1View)");
        },
        &loop);

    loop.Run();
  }

  return 0;
}
