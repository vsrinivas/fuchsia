// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/command.h"

#include <iostream>

namespace tracing {

Command::Command(sys::ComponentContext* context) : context_(context) {}

Command::~Command() = default;

sys::ComponentContext* Command::context() { return context_; }

sys::ComponentContext* Command::context() const { return context_; }

std::ostream& Command::out() {
  // Returning std::cerr on purpose. std::cout is redirected and consumed
  // by the enclosing context.
  return std::cerr;
}

std::istream& Command::in() { return std::cin; }

void Command::Run(const fxl::CommandLine& command_line, OnDoneCallback on_done) {
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

CommandWithController::CommandWithController(sys::ComponentContext* context)
    : Command(context), controller_(context->svc()->Connect<controller::Controller>()) {
  controller_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Trace controller disconnected unexpectedly";
    Done(EXIT_FAILURE);
  });
}

}  // namespace tracing
