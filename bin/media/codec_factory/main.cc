// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "codec_factory_app.h"

#include <lib/app/cpp/startup_context.h>
#include <lib/fsl/tasks/message_loop.h>

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  codec_factory::CodecFactoryApp app(
      fuchsia::sys::StartupContext::CreateFromStartupInfo(), &loop);

  loop.Run();

  return 0;
}
