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

#include "magma_util/application_context/application_context.h"
#include "apps/tracing/services/trace_controller.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace tracing {

class Command {
 public:
  struct Info {
    using CommandFactory =
        std::function<std::unique_ptr<Command>(faux::ApplicationContext*)>;

    CommandFactory factory;
    std::string name;
    std::string usage;
    std::map<std::string, std::string> options;
  };

  virtual ~Command();

  virtual void Run(const ftl::CommandLine& command_line) = 0;

 protected:
  static std::istream& in();
  static std::ostream& out();
  static std::ostream& err();

  explicit Command(faux::ApplicationContext* context);

  faux::ApplicationContext* context();
  faux::ApplicationContext* context() const;

 private:
  faux::ApplicationContext* context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(Command);
};

class CommandWithTraceController : public Command {
 protected:
  explicit CommandWithTraceController(faux::ApplicationContext* context);

  TraceControllerPtr& trace_controller();
  const TraceControllerPtr& trace_controller() const;

 private:
  std::unique_ptr<faux::ApplicationContext> context_;
  TraceControllerPtr trace_controller_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandWithTraceController);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMAND_H_
