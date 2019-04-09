// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/commands/list_categories.h"

#include <iostream>

#include "src/lib/fxl/logging.h"

namespace tracing {

Command::Info ListCategories::Describe() {
  return Command::Info{[](sys::ComponentContext* context) {
                         return std::make_unique<ListCategories>(context);
                       },
                       "list-categories",
                       "list all known categories",
                       {}};
}

ListCategories::ListCategories(sys::ComponentContext* context)
    : CommandWithController(context) {}

void ListCategories::Start(const fxl::CommandLine& command_line) {
  if (!(command_line.options().empty() &&
        command_line.positional_args().empty())) {
    FXL_LOG(ERROR) << "We encountered unknown options, please check your "
                   << "command invocation";
    Done(1);
    return;
  }

  trace_controller()->GetKnownCategories(
      [this](std::vector<fuchsia::tracing::controller::KnownCategory>
                 known_categories) {
        out() << "Known categories" << std::endl;
        for (const auto& it : known_categories) {
          out() << "  " << it.name << ": " << it.description << std::endl;
        }
        Done(0);
      });
}

}  // namespace tracing
