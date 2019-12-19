// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "src/developer/cmd/app.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  cmd::App app(loop.dispatcher());
  app.Init([&]() { loop.Quit(); });
  loop.Run();
  return 0;
}
