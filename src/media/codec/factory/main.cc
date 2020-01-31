// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

#include "codec_factory_app.h"

int main(int argc, char* argv[]) {
  syslog::InitLogger({"codec_factory"});
  
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  codec_factory::CodecFactoryApp app(&loop);

  loop.Run();

  return 0;
}
