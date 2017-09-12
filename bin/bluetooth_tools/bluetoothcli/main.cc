// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <linenoise.h>

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
  fsl::MessageLoop message_loop;

  bluetoothcli::App app;
  g_app = &app;

  linenoiseSetCompletionCallback(LinenoiseCompletionCallback);

  message_loop.task_runner()->PostTask([&app] { app.ReadNextInput(); });

  message_loop.Run();

  return EXIT_SUCCESS;
}
