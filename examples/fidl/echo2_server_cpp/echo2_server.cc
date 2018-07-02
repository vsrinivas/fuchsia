// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server_app.h"

#include <string>
#include <lib/async-loop/cpp/loop.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  bool quiet = (argc >= 2) && std::string("-q") == argv[1];
  echo2::EchoServerApp app(quiet);
  loop.Run();
  return 0;
}
