// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <linenoise/linenoise.h>

#include "app.h"

namespace {

bluetoothcli::App* g_app = nullptr;

void LinenoiseCompletionCallback(const char* buf, linenoiseCompletions* lc) {
  auto results = g_app->command_dispatcher().GetCommandsThatMatch(buf);
  for (const auto& result : results) {
    linenoiseAddCompletion(lc, result.c_str());
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  async::Loop loop;

  bluetoothcli::App app(loop.async(), [&loop](){loop.Quit();});
  g_app = &app;

  linenoiseSetCompletionCallback(LinenoiseCompletionCallback);

  async::PostTask(loop.async(), [&app] { app.ReadNextInput(); });

  loop.Run();

  return EXIT_SUCCESS;
}
