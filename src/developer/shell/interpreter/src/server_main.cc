// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/server.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, char** argv) {
  syslog::InitLogger({"shell", "interpreter"});

  shell::interpreter::server::Server server;
  if (!server.Listen()) {
    return 1;
  }
  server.Run();
  return 0;
}
