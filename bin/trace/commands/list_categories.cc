// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/src/trace/commands/list_categories.h"

#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

Command::Info ListCategories::Describe() {
  return Command::Info{[](app::ApplicationContext* context) {
                         return std::make_unique<ListCategories>(context);
                       },
                       "list-categories",
                       "list all known categories",
                       {}};
}

ListCategories::ListCategories(app::ApplicationContext* context)
    : CommandWithTraceController(context) {}

void ListCategories::Run(const fxl::CommandLine& command_line) {
  if (!(command_line.options().empty() &&
        command_line.positional_args().empty())) {
    err() << "We encountered unknown options, please check your "
          << "command invocation" << std::endl;
  }

  trace_controller()->GetKnownCategories(
      [](fidl::Map<fidl::String, fidl::String> known_categories) {
        out() << "Known categories" << std::endl;
        for (auto it = known_categories.begin(); it != known_categories.end();
             ++it) {
          out() << "  " << it.GetKey().get() << ": " << it.GetValue().get()
                << std::endl;
        }

        mtl::MessageLoop::GetCurrent()->QuitNow();
      });
}

}  // namespace tracing
