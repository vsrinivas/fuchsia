// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/examples/ui/tile/tile_view.h"
#include "lib/fxl/command_line.h"
#include "lib/ui/view_framework/view_provider_app.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  examples::TileParams params;
  if (!params.Parse(command_line)) {
    FXL_LOG(ERROR) << "Missing or invalid URL parameters.  See README.";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.dispatcher());

  mozart::ViewProviderApp app([&params](mozart::ViewContext view_context) {
    return std::make_unique<examples::TileView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.startup_context, params);
  });

  loop.Run();
  return 0;
}
