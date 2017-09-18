// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <ostream>

#include "apps/tracing/src/trace/app.h"

#include "apps/tracing/src/trace/commands/dump_provider.h"
#include "apps/tracing/src/trace/commands/list_categories.h"
#include "apps/tracing/src/trace/commands/list_providers.h"
#include "apps/tracing/src/trace/commands/record.h"

namespace tracing {

App::App(app::ApplicationContext* context) : Command(context) {
  RegisterCommand(DumpProvider::Describe());
  RegisterCommand(ListCategories::Describe());
  RegisterCommand(ListProviders::Describe());
  RegisterCommand(Record::Describe());
}

App::~App() {}

void App::Run(const fxl::CommandLine& command_line) {
  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(0);
  }

  const auto& positional_args = command_line.positional_args();

  if (positional_args.empty()) {
    err() << "Command missing - aborting" << std::endl;
    PrintHelp();
    exit(1);
  }

  auto it = known_commands_.find(positional_args.front());
  if (it == known_commands_.end()) {
    err() << "Unknown command '" << positional_args.front() << "' - aborting"
          << std::endl;
    PrintHelp();
    exit(1);
  }

  if (!context()->has_environment_services()) {
    err() << "Cannot access application environment services" << std::endl;
    exit(1);
  }

  command_ = it->second.factory(context());
  command_->Run(fxl::CommandLineFromIteratorsWithArgv0(
      positional_args.front(), positional_args.begin() + 1,
      positional_args.end()));
}

void App::RegisterCommand(Command::Info info) {
  known_commands_[info.name] = std::move(info);
}

void App::PrintHelp() {
  out() << "trace [options] command [command-specific options]" << std::endl;
  out() << "  --help: Produce this help message" << std::endl << std::endl;
  for (const auto& pair : known_commands_) {
    out() << "  " << pair.second.name << " - " << pair.second.usage
          << std::endl;
    for (const auto& option : pair.second.options)
      out() << "    --" << option.first << ": " << option.second << std::endl;
  }
}

}  // namespace tracing
