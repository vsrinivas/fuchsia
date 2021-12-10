// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/monitors.h"

#include <lib/sync/completion.h>
#include <stddef.h>
#include <threads.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

MonitorClients::MonitorClients() { allow_add_.Signal(); }

MonitorClients::~MonitorClients() { CloseAll(); }

// Adds a subscriber for status updates.
void MonitorClients::Add(fidl::InterfaceHandle<Monitor> monitor) {
  MonitorPtr ptr;
  ptr.Bind(std::move(monitor), dispatcher_.get());
  // If a call to |Finish| is being performed, wait for it to complete.
  allow_add_.WaitFor("a call to `Finish` to complete");
  dispatcher_.PostTask(
      [this, ptr = std::move(ptr)]() mutable { monitors_.AddInterfacePtr(std::move(ptr)); });
}

Status MonitorClients::GetStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  return CopyStatus(status_);
}

void MonitorClients::SetStatus(Status status) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_ = std::move(status);
}

void MonitorClients::Update(UpdateReason reason) {
  if (reason == UpdateReason::DONE) {
    Finish();
    return;
  }
  auto status = GetStatus();
  dispatcher_.PostTask([this, reason, status = std::move(status)]() {
    for (auto& ptr : monitors_.ptrs()) {
      (*ptr)->Update(reason, CopyStatus(status), []() {});
    }
  });
}

void MonitorClients::Finish() {
  size_t num_responses = 0;
  SyncWait finished;
  auto status = GetStatus();
  dispatcher_.PostTask([this, &num_responses, &finished, status = std::move(status)]() {
    // Prevent new additions.
    allow_add_.Reset();
    if (monitors_.size() == 0) {
      finished.Signal();
      return;
    }
    for (auto& ptr : monitors_.ptrs()) {
      (*ptr)->Update(UpdateReason::DONE, CopyStatus(status), [this, &num_responses, &finished]() {
        ++num_responses;
        if (num_responses >= monitors_.size()) {
          finished.Signal();
        }
      });
    }
  });
  // If a monitor closes its channel or otherwise encounters an error concurrently with the call to
  // |Update| above, |finished| may not be signalled. In this event, just close this end of the
  // channel after a short duration; at worst a single status message is lost.
  finished.TimedWait(zx::sec(1));
  CloseAll();
  allow_add_.Signal();
}

void MonitorClients::CloseAll() {
  allow_add_.Reset();
  if (thrd_equal(dispatcher_.thrd(), thrd_current())) {
    monitors_.CloseAll();
    return;
  }
  SyncWait sync;
  dispatcher_.PostTask([this, &sync]() {
    monitors_.CloseAll();
    sync.Signal();
  });
  sync.WaitFor("monitors to close");
}

}  // namespace fuzzing
