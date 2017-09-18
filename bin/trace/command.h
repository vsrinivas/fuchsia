// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_COMMAND_H_
#define APPS_TRACING_SRC_TRACE_COMMAND_H_

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/tracing/fidl/trace_controller.fidl.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace tracing {

class Command {
 public:
  struct Info {
    using CommandFactory =
        std::function<std::unique_ptr<Command>(app::ApplicationContext*)>;

    CommandFactory factory;
    std::string name;
    std::string usage;
    std::map<std::string, std::string> options;
  };

  virtual ~Command();

  virtual void Run(const fxl::CommandLine& command_line) = 0;

 protected:
  static std::istream& in();
  static std::ostream& out();
  static std::ostream& err();

  explicit Command(app::ApplicationContext* context);

  app::ApplicationContext* context();
  app::ApplicationContext* context() const;

 private:
  app::ApplicationContext* context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Command);
};

class CommandWithTraceController : public Command {
 protected:
  explicit CommandWithTraceController(app::ApplicationContext* context);

  TraceControllerPtr& trace_controller();
  const TraceControllerPtr& trace_controller() const;

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  TraceControllerPtr trace_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandWithTraceController);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMAND_H_
