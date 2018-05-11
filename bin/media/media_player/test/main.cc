// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/media_player/test/media_player_test_unattended.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  media_player::test::MediaPlayerTestUnattended app(
      [&loop]() { async::PostTask(loop.async(), [&loop]() { loop.Quit(); }); });

  loop.Run();
  return 0;
}
