// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/util/commands.h"
#include "src/connectivity/network/mdns/util/mdns_impl.h"
#include "src/lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"mdns-util"});

  mdns::CommandParser parser(argc, argv);
  mdns::Command command = parser.Parse();
  switch (command.verb()) {
    case mdns::CommandVerb::kHelp:
    case mdns::CommandVerb::kMalformed:
      mdns::Command::ShowHelp(command.help_verb());
      return 0;
    default:
      break;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  mdns::MdnsImpl impl(component_context.get(), std::move(command), loop.dispatcher(), [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  loop.Run();
  return 0;
}
