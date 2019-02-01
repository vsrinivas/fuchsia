// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_COMMAND_H_
#define GARNET_BIN_TRACE_COMMAND_H_

#include <iosfwd>
#include <map>
#include <memory>
#include <string>

#include <fuchsia/tracing/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace tracing {

class Command {
 public:
  // OnDoneCallback is the callback type invoked when a command finished
  // running. It takes as argument the return code to exit the process with.
  using OnDoneCallback = fit::function<void(int32_t)>;
  struct Info {
    using CommandFactory =
        fit::function<std::unique_ptr<Command>(component::StartupContext*)>;

    CommandFactory factory;
    std::string name;
    std::string usage;
    std::map<std::string, std::string> options;
  };

  virtual ~Command();

  void Run(const fxl::CommandLine& command_line, OnDoneCallback on_done);

 protected:
  static std::ostream& out();

  explicit Command(component::StartupContext* context);

  component::StartupContext* context();
  component::StartupContext* context() const;

  // Starts running the command.
  // The command must invoke Done() when finished.
  virtual void Start(const fxl::CommandLine& command_line) = 0;
  void Done(int32_t return_code);

 private:
  component::StartupContext* context_;
  OnDoneCallback on_done_;
  int32_t return_code_ = -1;

  FXL_DISALLOW_COPY_AND_ASSIGN(Command);
};

class CommandWithTraceController : public Command {
 protected:
  explicit CommandWithTraceController(component::StartupContext* context);

  fuchsia::tracing::TraceControllerPtr& trace_controller();
  const fuchsia::tracing::TraceControllerPtr& trace_controller() const;

 private:
  std::unique_ptr<component::StartupContext> context_;
  fuchsia::tracing::TraceControllerPtr trace_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandWithTraceController);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_COMMAND_H_
