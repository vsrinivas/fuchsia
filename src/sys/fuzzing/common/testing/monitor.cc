// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/monitor.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeMonitor::FakeMonitor(ExecutorPtr executor) : binding_(this), executor_(std::move(executor)) {}

fidl::InterfaceHandle<Monitor> FakeMonitor::NewBinding() {
  return binding_.NewBinding(executor_->dispatcher());
}

void FakeMonitor::Update(UpdateReason reason, Status status, UpdateCallback callback) {
  updates_.emplace_back();
  auto& back = updates_.back();
  back.reason = reason;
  back.status = std::move(status);
  task_.resume_task();
  callback();
}

Promise<> FakeMonitor::AwaitUpdate() {
  return fpromise::make_promise([this](Context& context) -> Result<> {
           if (updates_.empty()) {
             task_ = context.suspend_task();
             return fpromise::pending();
           }
           return fpromise::ok();
         })
      .wrap_with(scope_);
}

}  // namespace fuzzing
