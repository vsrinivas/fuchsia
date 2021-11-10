// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <string>

#include "echo_server_app.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  bool quiet = (argc >= 2) && std::string("-q") == argv[1];
  echo::EchoServerApp app(quiet);
  loop.Run();
  return 0;
}
