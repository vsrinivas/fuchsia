// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include "local_codec_factory.h"

int main(int argc, char* argv[]) {
  ZX_DEBUG_ASSERT(argc == 1);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  codec_factory::LocalCodecFactory::CreateSelfOwned(loop.dispatcher());

  loop.Run();

  return 0;
}
