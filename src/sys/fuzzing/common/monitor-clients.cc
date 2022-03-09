// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/monitor-clients.h"

#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

using ::fuchsia::fuzzer::MonitorPtr;

MonitorClients::MonitorClients(ExecutorPtr executor) : executor_(std::move(executor)) {}

void MonitorClients::Add(fidl::InterfaceHandle<Monitor> monitor) {
  MonitorPtr ptr;
  ptr.Bind(std::move(monitor), executor_->dispatcher());
  monitors_.AddInterfacePtr(std::move(ptr));
}

void MonitorClients::Update(UpdateReason reason) {
  std::vector<Promise<>> promise_vector;
  for (auto& ptr : monitors_.ptrs()) {
    Bridge<> bridge;
    (*ptr)->Update(reason, CopyStatus(status_), bridge.completer.bind());
    promise_vector.emplace_back(bridge.consumer.promise_or(fpromise::error()));
  }
  auto promises = fpromise::join_promise_vector(std::move(promise_vector));
  auto previous = std::move(previous_);
  Bridge<> bridge;
  previous_ = std::move(bridge.consumer);
  auto task = (previous ? previous.promise().and_then(std::move(promises)).box() : promises.box())
                  .and_then([this, reason, completer = std::move(bridge.completer)](
                                const std::vector<Result<>>& results) mutable {
                    for (const auto& result : results) {
                      if (result.is_error()) {
                        FX_LOGS(WARNING) << "Failed to update monitor.";
                      }
                    }
                    if (reason == UpdateReason::DONE) {
                      CloseAll();
                    }
                    completer.complete_ok();
                    return fpromise::ok();
                  })
                  .wrap_with(scope_);
  executor_->schedule_task(std::move(task));
}

Promise<> MonitorClients::AwaitAcknowledgement() {
  auto previous = std::move(previous_);
  return previous ? previous.promise().box() : fpromise::make_ok_promise().box();
}

void MonitorClients::CloseAll() { monitors_.CloseAll(); }

}  // namespace fuzzing
