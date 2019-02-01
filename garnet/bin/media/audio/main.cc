// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/startup_context.h"

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/audio/audio_core_client.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  // StartupContext is safe to initialize early as we publish all implemented
  // interfaces before we run the event loop.
  auto ctx = component::StartupContext::CreateFromStartupInfo();

  auto closer = [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  };
  media::audio::AudioCoreClient audio_core(ctx.get(), closer);

  loop.Run();
  return 0;
}
