// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>
#include <vector>

namespace fuzzing {

LLVMTargetAdapter::LLVMTargetAdapter(ExecutorPtr executor)
    : binding_(this), executor_(executor), eventpair_(executor) {}

fidl::InterfaceRequestHandler<TargetAdapter> LLVMTargetAdapter::GetHandler() {
  return [this](fidl::InterfaceRequest<TargetAdapter> request) {
    binding_.Bind(std::move(request), executor_->dispatcher());
  };
}

void LLVMTargetAdapter::SetParameters(const std::vector<std::string>& parameters) {
  parameters_ = std::vector<std::string>(parameters.begin(), parameters.end());
}

void LLVMTargetAdapter::GetParameters(GetParametersCallback callback) {
  callback(std::vector<std::string>(parameters_.begin(), parameters_.end()));
}

void LLVMTargetAdapter::Connect(zx::eventpair eventpair, Buffer test_input,
                                ConnectCallback callback) {
  test_input_.LinkReserved(std::move(test_input));
  test_input_.SetPoisoning(true);
  eventpair_.Pair(std::move(eventpair));
  suspended_.resume_task();
  callback();
}

Promise<> LLVMTargetAdapter::Run() {
  return fpromise::make_promise([this](Context& context) -> Result<> {
           if (!eventpair_.IsConnected()) {
             suspended_ = context.suspend_task();
             return fpromise::pending();
           }
           return fpromise::ok();
         })
      .and_then([this, start = ZxFuture<zx_signals_t>()](Context& context) mutable -> Result<> {
        while (true) {
          if (!start) {
            start = eventpair_.WaitFor(kStart);
          }
          if (!start(context)) {
            return fpromise::pending();
          }
          if (start.is_error()) {
            return fpromise::ok();
          }
          auto status = eventpair_.SignalSelf(start.take_value(), 0);
          if (status != ZX_OK) {
            FX_LOGS(WARNING) << "Engine disconnected unexpectedly.";
            return fpromise::error();
          }
          auto result = LLVMFuzzerTestOneInput(test_input_.data(), test_input_.size());
          if (result) {
            FX_LOGS(FATAL) << "Fuzz target function returned non-zero result: " << result;
          }
          status = eventpair_.SignalPeer(0, kFinish);
          if (status != ZX_OK) {
            FX_LOGS(WARNING) << "Engine disconnected unexpectedly.";
            return fpromise::error();
          }
        }
      })
      .wrap_with(scope_);
}

}  // namespace fuzzing
