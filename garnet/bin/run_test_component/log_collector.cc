// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/log_collector.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

namespace run {

LogCollector::LogCollector(Callback callback) : callback_(std::move(callback)), binding_(this) {}

void LogCollector::Log(fuchsia::logger::LogMessage log, LogCallback received) {
  callback_(std::move(log));
  received();
}

void LogCollector::LogMany(::std::vector<fuchsia::logger::LogMessage> logs,
                           LogManyCallback received) {
  for (auto& log : logs) {
    callback_(std::move(log));
  }
  received();
}

void LogCollector::Done() {
  // do nothing
}

zx_status_t LogCollector::Bind(fidl::InterfaceRequest<fuchsia::logger::LogListenerSafe> request,
                               async_dispatcher_t* dispatcher) {
  if (binding_.is_bound()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  auto status = binding_.Bind(std::move(request), dispatcher);
  if (status == ZX_OK) {
    binding_.set_error_handler([&](zx_status_t) {
      for (auto& c : unbind_callbacks_) {
        c();
      }
      unbind_callbacks_.clear();
    });
  }
  return status;
}

void LogCollector::NotifyOnUnBind(fit::function<void()> callback) {
  if (!binding_.is_bound()) {
    callback();
    return;
  }
  unbind_callbacks_.push_back(std::move(callback));
}

}  // namespace run
