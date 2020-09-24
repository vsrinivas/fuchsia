// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/sounds/soundplayer/sound_player_impl.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"soundplayer"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  component_context->outgoing()->AddPublicService<fuchsia::media::sounds::Player>(
      [component_context = component_context.get()](
          fidl::InterfaceRequest<fuchsia::media::sounds::Player> request) {
        fuchsia::media::AudioPtr audio_service =
            component_context->svc()->Connect<fuchsia::media::Audio>();
        new soundplayer::SoundPlayerImpl(std::move(audio_service), std::move(request));
      });

  loop.Run();
  return 0;
}
