// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"

#include <lib/async/cpp/task.h>
#include "lib/fxl/command_line.h"

#include "app.h"

int main(int argc, char* argv[]) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  bool just_tilts = command_line.HasOption("tilt");

  fsl::MessageLoop message_loop;

  bt_beacon_reader::App app(just_tilts);

  async::PostTask(message_loop.async(), [&app] { app.StartScanning(); });

  message_loop.Run();

  return EXIT_SUCCESS;
}
