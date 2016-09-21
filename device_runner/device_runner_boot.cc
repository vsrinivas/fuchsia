// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A bootstrapping application that parses command line arguments and starts up
// the TQ runtime flow. It also sets configutaion information for dummy
// components like dummmy-device-shell and dummy-user-shell.

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/application_manager/application_manager.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    FTL_DLOG(INFO) << "mojo:device-shell expects 2 additional arguments.\n"
                   << "Usage: device_shell_boot [user] [recipe]";
    return 1;
  }

  std::unordered_map<std::string, std::vector<std::string>> args_for{
      {"mojo:dummy_device_shell", {argv[1]}},
      {"mojo:dummy_user_shell", {argv[2]}},
  };
  mojo::ApplicationManager manager(std::move(args_for));

  mtl::MessageLoop message_loop;
  message_loop.task_runner()->PostTask([&manager]() {
    if (!manager.StartInitialApplication("mojo:device_runner")) {
      exit(1);
    }
  });

  message_loop.Run();
  return 0;
}
