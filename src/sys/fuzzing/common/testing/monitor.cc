// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/monitor.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeMonitor::FakeMonitor() : binding_(this) {}

fidl::InterfaceHandle<Monitor> FakeMonitor::NewBinding() { return binding_.NewBinding(); }

MonitorPtr FakeMonitor::Bind(const std::shared_ptr<Dispatcher>& dispatcher) {
  MonitorPtr ptr;
  binding_.Bind(ptr.NewRequest(dispatcher->get()));
  return ptr;
}

void FakeMonitor::Update(UpdateReason reason, Status status, UpdateCallback callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reasons_.push_back(reason);
    statuses_.push_back(std::move(status));
    sync_.Signal();
  }
  callback();
}

UpdateReason FakeMonitor::NextReason() {
  UpdateReason reason;
  NextStatus(&reason);
  return reason;
}

Status FakeMonitor::NextStatus(UpdateReason* out_reason) {
  sync_.WaitFor("next status update");
  UpdateReason reason;
  Status status;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reason = reasons_.front();
    reasons_.pop_front();
    status = std::move(statuses_.front());
    statuses_.pop_front();
    if (reasons_.empty()) {
      sync_.Reset();
    }
  }
  if (out_reason) {
    *out_reason = reason;
  }
  return status;
}

}  // namespace fuzzing
