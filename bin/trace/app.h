// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_APP_H_
#define APPS_TRACING_SRC_TRACE_APP_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "apps/tracing/src/trace/command.h"
#include "lib/fxl/macros.h"

namespace tracing {

class App : public Command {
 public:
  App(app::ApplicationContext* context);
  ~App();

  void Run(const fxl::CommandLine& command_line) override;

 private:
  void RegisterCommand(Command::Info info);
  void PrintHelp();

  std::map<std::string, Command::Info> known_commands_;
  std::unique_ptr<Command> command_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_APP_H_
