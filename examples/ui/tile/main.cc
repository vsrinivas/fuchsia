// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "garnet/examples/ui/tile/tile_view.h"
#include "lib/ui/view_framework/view_provider_app.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  examples::TileParams params;
  if (!params.Parse(command_line)) {
    FTL_LOG(ERROR) << "Missing or invalid URL parameters.  See README.";
    return 1;
  }

  mtl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  mozart::ViewProviderApp app([&params](mozart::ViewContext view_context) {
    return std::make_unique<examples::TileView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context, params);
  });

  loop.Run();
  return 0;
}
