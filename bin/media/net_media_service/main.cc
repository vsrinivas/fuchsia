// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/net_media_service/net_media_service_impl.h"
#include "lib/component/cpp/startup_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  media_player::NetMediaServiceImpl impl(
      component::StartupContext::CreateFromStartupInfo());

  loop.Run();
  return 0;
}
