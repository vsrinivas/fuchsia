// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace-provider/provider.h>
#include <lib/ui/base_view/cpp/view_provider_component_transitional.h>

#include "garnet/examples/ui/tile/tile_view.h"
#include "src/lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  examples::TileParams tile_params;
  if (!tile_params.Parse(command_line)) {
    FXL_LOG(ERROR) << "Missing or invalid URL parameters.  See README.";
    return 1;
  }

  scenic::ViewProviderComponentTransitional component(
      [params = std::move(tile_params)](scenic::ViewContextTransitional context) {
        return std::make_unique<examples::TileView>(std::move(context), std::move(params));
      },
      &loop);

  loop.Run();
  return 0;
}
