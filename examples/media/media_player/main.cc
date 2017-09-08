// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_view.h"
#include "lib/ui/view_framework/view_provider_app.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  examples::MediaPlayerParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  mtl::MessageLoop loop;

  mozart::ViewProviderApp app([&params](mozart::ViewContext view_context) {
    return std::make_unique<examples::MediaPlayerView>(
        std::move(view_context.view_manager),
        std::move(view_context.view_owner_request),
        view_context.application_context, params);
  });

  loop.Run();
  return 0;
}
