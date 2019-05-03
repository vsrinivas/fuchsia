// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <trace-provider/provider.h>

#include "garnet/bin/developer/tiles/tiles.h"

void Usage() {
  printf(
      "Usage: tiles [--border=...]\n"
      "\n"
      "Tiles displays a set of views as tiles. Add or remove tiles with\n"
      "the 'tiles_ctl' command line utility or connecting to the\n"
      "fuchsia.developer.tiles.Tiles FIDL API exposed by this program\n"
      "\n"
      "Options:\n"
      "  --border=<integer>  Border (in pixels) around each tile\n"
      "  --input_path=<string>  DEPRECATED - Flag to be removed\n");
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("h") || command_line.HasOption("help")) {
    Usage();
    return 0;
  }

  auto border_arg = command_line.GetOptionValueWithDefault("border", "10");
  int border = fxl::StringToNumber<int>(border_arg);

  if (command_line.HasOption("input_path", nullptr)) {
    // Ease users off this flag.
    FXL_LOG(ERROR)
        << "The --input_path= flag is DEPRECATED. Flag will be removed.";
  }

  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  auto scenic =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();

  // Create tiles with a token for its root view.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  scenic::ViewContext view_context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
      .view_token = std::move(view_token),
      .incoming_services = {},
      .outgoing_services = {},
      .startup_context = startup_context.get(),
  };
  tiles::Tiles tiles(std::move(view_context), command_line.positional_args(),
                     border);

  // Ask the presenter to display it.
  auto presenter =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>();
  presenter->PresentView(std::move(view_holder_token), nullptr);

  loop.Run();
  return 0;
}
