// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace/command.h"

namespace tracing {

Command::Command(component::StartupContext* context) : context_(context) {}

Command::~Command() = default;

component::StartupContext* Command::context() { return context_; }

component::StartupContext* Command::context() const { return context_; }

std::ostream& Command::out() {
  // Returning std::cerr on purpose. std::cout is redirected and consumed
  // by the enclosing context.
  return std::cerr;
}

void Command::Run(const fxl::CommandLine& command_line,
                  OnDoneCallback on_done) {
  if (return_code_ >= 0) {
    on_done(return_code_);
  } else {
    on_done_ = std::move(on_done);
    Start(command_line);
  }
}

void Command::Done(int32_t return_code) {
  return_code_ = return_code;
  if (on_done_) {
    on_done_(return_code_);
    on_done_ = nullptr;
  }
}

CommandWithTraceController::CommandWithTraceController(
    component::StartupContext* context)
    : Command(context),
      trace_controller_(
          context->ConnectToEnvironmentService<fuchsia::tracing::TraceController>()) {
  trace_controller_.set_error_handler([this] {
    FXL_LOG(ERROR) << "Trace controller disconnected unexpectedly";
    Done(1);
  });
}

fuchsia::tracing::TraceControllerPtr& CommandWithTraceController::trace_controller() {
  return trace_controller_;
}

const fuchsia::tracing::TraceControllerPtr& CommandWithTraceController::trace_controller() const {
  return trace_controller_;
}

}  // namespace tracing
