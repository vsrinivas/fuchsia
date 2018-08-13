// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/mediaplayer/test/media_player_test_params.h"
#include "garnet/bin/mediaplayer/test/media_player_test_unattended.h"
#include "garnet/bin/mediaplayer/test/media_player_test_view.h"
#include "lib/fxl/command_line.h"
#include "lib/ui/view_framework/view_provider_app.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  media_player::test::MediaPlayerTestParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  int result;
  auto quit_callback = [&loop, &result](int exit_code) {
    result = exit_code;
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  };

  if (params.unattended()) {
    media_player::test::MediaPlayerTestUnattended app(quit_callback);
    loop.Run();
  } else {
    mozart::ViewProviderApp app(
        [&loop, &params, quit_callback](mozart::ViewContext view_context) {
          return std::make_unique<media_player::test::MediaPlayerTestView>(
              quit_callback, std::move(view_context.view_manager),
              std::move(view_context.view_owner_request),
              view_context.startup_context, params);
        });

    loop.Run();
  }

  return result;
}
