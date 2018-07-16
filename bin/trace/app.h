// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_APP_H_
#define GARNET_BIN_TRACE_APP_H_

#include <map>
#include <memory>
#include <string>

#include "garnet/bin/trace/command.h"
#include "lib/fxl/macros.h"

namespace tracing {

class App : public Command {
 public:
  App(component::StartupContext* context);
  ~App();

 protected:
  void Start(const fxl::CommandLine& command_line) override;

 private:
  void RegisterCommand(Command::Info info);
  void PrintHelp();

  std::map<std::string, Command::Info> known_commands_;
  std::unique_ptr<Command> command_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_APP_H_
