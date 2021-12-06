// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/monitors.h"

#include <lib/sync/completion.h>
#include <stddef.h>
#include <threads.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

MonitorClients::MonitorClients() { sync_completion_signal(&allow_add_); }

MonitorClients::~MonitorClients() { CloseAll(); }

// Adds a subscriber for status updates.
void MonitorClients::Add(fidl::InterfaceHandle<Monitor> monitor) {
  MonitorPtr ptr;
  ptr.Bind(std::move(monitor), dispatcher_.get());
  // If a call to |Finish| is being performed, wait for it to complete.
  sync_completion_wait(&allow_add_, ZX_TIME_INFINITE);
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
  sync_completion_t finished;
  auto status = GetStatus();
  dispatcher_.PostTask([this, &num_responses, &finished, status = std::move(status)]() {
    // Prevent new additions.
    sync_completion_reset(&allow_add_);
    if (monitors_.size() == 0) {
      sync_completion_signal(&finished);
      return;
    }
    for (auto& ptr : monitors_.ptrs()) {
      (*ptr)->Update(UpdateReason::DONE, CopyStatus(status), [this, &num_responses, &finished]() {
        ++num_responses;
        if (num_responses >= monitors_.size()) {
          sync_completion_signal(&finished);
        }
      });
    }
  });
  // If a monitor closes its channel or otherwise encounters an error concurrently with the call to
  // |Update| above, |finished| may not be signalled. In this event, just close this end of the
  // channel after a short duration; at worst a single status message is lost.
  sync_completion_wait(&finished, ZX_SEC(1));
  CloseAll();
  sync_completion_signal(&allow_add_);
}

void MonitorClients::CloseAll() {
  sync_completion_reset(&allow_add_);
  if (thrd_equal(dispatcher_.thrd(), thrd_current())) {
    monitors_.CloseAll();
    return;
  }
  sync_completion_t sync;
  dispatcher_.PostTask([this, &sync]() {
    monitors_.CloseAll();
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
}

}  // namespace fuzzing
