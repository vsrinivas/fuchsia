// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/adapter.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeTargetAdapter::FakeTargetAdapter(ExecutorPtr executor)
    : binding_(this), executor_(executor), eventpair_(executor) {}

fidl::InterfaceRequestHandler<TargetAdapter> FakeTargetAdapter::GetHandler() {
  return [this](fidl::InterfaceRequest<TargetAdapter> request) {
    eventpair_.Reset();
    binding_.Bind(std::move(request), executor_->dispatcher());
  };
}

void FakeTargetAdapter::SetParameters(const std::vector<std::string>& parameters) {
  parameters_ = std::vector<std::string>(parameters.begin(), parameters.end());
}

void FakeTargetAdapter::GetParameters(GetParametersCallback callback) {
  callback(std::vector<std::string>(parameters_.begin(), parameters_.end()));
}

void FakeTargetAdapter::Connect(zx::eventpair eventpair, zx::vmo test_input,
                                ConnectCallback callback) {
  eventpair_.Pair(std::move(eventpair));
  if (auto status = test_input_.Link(std::move(test_input)); status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to link test input: " << zx_status_get_string(status);
  }
  suspended_.resume_task();
  callback();
}

ZxPromise<Input> FakeTargetAdapter::TestOneInput() {
  return AwaitStart()
      .and_then([this](Input& input) -> ZxResult<Input> {
        auto status = Finish();
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        return fpromise::ok(std::move(input));
      })
      .wrap_with(scope_);
}

ZxPromise<Input> FakeTargetAdapter::AwaitStart() {
  return fpromise::make_promise([this](Context& context) -> ZxResult<> {
           if (eventpair_.IsConnected()) {
             return fpromise::ok();
           }
           suspended_ = context.suspend_task();
           return fpromise::pending();
         })
      .and_then(eventpair_.WaitFor(kStart))
      .and_then([this](const zx_signals_t& observed) -> ZxResult<Input> {
        auto input = Input(test_input_);
        auto status = eventpair_.SignalSelf(kStart, 0);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        return fpromise::ok(std::move(input));
      })
      .wrap_with(scope_);
}

zx_status_t FakeTargetAdapter::Finish() { return eventpair_.SignalPeer(0, kFinish); }

ZxPromise<> FakeTargetAdapter::AwaitDisconnect() {
  return eventpair_.WaitFor(ZX_EVENTPAIR_PEER_CLOSED)
      .then([](const ZxResult<zx_signals_t>& result) -> ZxResult<> {
        FX_DCHECK(result.is_error());
        auto status = result.error();
        if (status != ZX_ERR_PEER_CLOSED) {
          return fpromise::error(status);
        }
        return fpromise::ok();
      });
}

}  // namespace fuzzing
