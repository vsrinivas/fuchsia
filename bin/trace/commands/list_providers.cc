// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace/commands/list_providers.h"

#include "lib/fsl/tasks/message_loop.h"

namespace tracing {

Command::Info ListProviders::Describe() {
  return Command::Info{[](app::ApplicationContext* context) {
                         return std::make_unique<ListProviders>(context);
                       },
                       "list-providers",
                       "list all registered providers",
                       {}};
}

ListProviders::ListProviders(app::ApplicationContext* context)
    : CommandWithTraceController(context) {}

void ListProviders::Run(const fxl::CommandLine& command_line) {
  if (!(command_line.options().empty() &&
        command_line.positional_args().empty())) {
    err() << "We encountered unknown options, please check your "
          << "command invocation" << std::endl;
  }

  trace_controller()->GetRegisteredProviders(
      [](fidl::Array<TraceProviderInfoPtr> providers) {
        out() << "Registered providers" << std::endl;
        for (const auto& provider : providers) {
          out() << "  #" << provider->id << ": '" << provider->label << "'"
                << std::endl;
        }
        fsl::MessageLoop::GetCurrent()->QuitNow();
      });
}

}  // namespace tracing
