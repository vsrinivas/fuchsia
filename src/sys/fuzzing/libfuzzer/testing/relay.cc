// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/testing/relay.h"

#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

RelayImpl::RelayImpl() : executor_(MakeExecutor(loop_.dispatcher())) {}

fidl::InterfaceRequestHandler<Relay> RelayImpl::GetHandler() {
  return [this](fidl::InterfaceRequest<Relay> request) {
    bindings_.AddBinding(this, std::move(request), executor_->dispatcher());
  };
}

zx_status_t RelayImpl::Run() { return loop_.Run(); }

void RelayImpl::SetTestData(SignaledBuffer test_data, SetTestDataCallback callback) {
  auto completer = std::move(completer_);
  if (!completer) {
    Bridge<SignaledBuffer> test_data_bridge;
    completer = std::move(test_data_bridge.completer);
    consumer_ = std::move(test_data_bridge.consumer);
  }
  Bridge<> finish_bridge;
  finish_ = std::move(finish_bridge.completer);
  executor_->schedule_task(
      finish_bridge.consumer.promise().and_then([callback = std::move(callback)] { callback(); }));
  completer.complete_ok(std::move(test_data));
}

void RelayImpl::WatchTestData(WatchTestDataCallback callback) {
  auto consumer = std::move(consumer_);
  if (!consumer) {
    Bridge<SignaledBuffer> test_data_bridge;
    completer_ = std::move(test_data_bridge.completer);
    consumer = std::move(test_data_bridge.consumer);
  }
  executor_->schedule_task(
      consumer.promise().and_then([callback = std::move(callback)](SignaledBuffer& test_data) {
        callback(std::move(test_data));
      }));
}

void RelayImpl::Finish() { finish_.complete_ok(); }

}  // namespace fuzzing
