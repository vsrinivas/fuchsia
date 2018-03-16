// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace/command.h"

namespace tracing {

Command::Command(component::ApplicationContext* context) : context_(context) {}

Command::~Command() = default;

component::ApplicationContext* Command::context() { return context_; }

component::ApplicationContext* Command::context() const { return context_; }

std::istream& Command::in() {
  return std::cin;
}

std::ostream& Command::out() {
  // Returning std::cerr on purpose. std::cout is redirected and consumed
  // by the enclosing context.
  return std::cerr;
}

std::ostream& Command::err() {
  return std::cerr;
}

CommandWithTraceController::CommandWithTraceController(
    component::ApplicationContext* context)
    : Command(context),
      trace_controller_(
          context->ConnectToEnvironmentService<TraceController>()) {}

TraceControllerPtr& CommandWithTraceController::trace_controller() {
  return trace_controller_;
}

const TraceControllerPtr& CommandWithTraceController::trace_controller() const {
  return trace_controller_;
}

}  // namespace tracing
