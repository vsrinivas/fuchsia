// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>

#include "codec_factory_app.h"

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  codec_factory::CodecFactoryApp app(
      fuchsia::sys::StartupContext::CreateFromStartupInfo(), &loop);

  loop.Run();

  return 0;
}
