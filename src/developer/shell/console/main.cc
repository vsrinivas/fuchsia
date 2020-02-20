// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "lib/async-loop/default.h"
#include "src/developer/shell/console/app.h"
#include "src/developer/shell/console/scoped_interpreter.h"

namespace shell::console {

int ConsoleMain(int argc, const char** argv) {
  ScopedInterpreter interpreter;

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  shell::console::App app(interpreter.client(), loop.dispatcher());
  if (!app.Init(argc, argv, [&] { loop.Quit(); })) {
    return -1;
  }
  loop.Run();
  return 0;
}
}  // namespace shell::console

int main(int argc, const char** argv) { return shell::console::ConsoleMain(argc, argv); }
