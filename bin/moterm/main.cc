// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/moterm_params.h"
#include "apps/moterm/moterm_view.h"
#include "apps/mozart/lib/skia/skia_font_loader.h"
#include "apps/mozart/lib/view_framework/view_provider_app.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/log_settings.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  moterm::MotermParams params;
  if (!ftl::SetLogSettingsFromCommandLine(command_line) ||
      !params.Parse(command_line)) {
    FTL_LOG(ERROR) << "Missing or invalid parameters. See README.";
    return 1;
  }

  mtl::MessageLoop loop;

  mozart::ViewProviderApp app([&params](mozart::ViewContext view_context) {
    return std::make_unique<moterm::MotermView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context, params);
  });

  loop.Run();
  return 0;
}
