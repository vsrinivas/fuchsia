// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {
namespace {

// This enum is used with |Pend| to schedule specific actions.
enum Action : uint8_t {
  kExecute,
  kMinimize,
  kCleanse,
  kFuzz,
  kMerge,
  kStop,
};

}  // namespace

Runner::Runner() : action_(kStop) {
  // Start the worker and ensure is up and running.
  worker_ = std::thread([this]() { Worker(); });
  bool idle = false;
  do {
    std::this_thread::yield();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      idle = idle_;
    }
  } while (!idle);
}

Runner::~Runner() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    action_ = kStop;
  }
  sync_completion_signal(&worker_sync_);
  worker_.join();
}

zx_status_t Runner::Configure(const std::shared_ptr<Options>& options) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!idle_) {
    FX_LOGS(WARNING) << "Attempted to re-configure when fuzzing already in progress.";
    return ZX_ERR_BAD_STATE;
  }
  ConfigureImpl(options);
  return ZX_OK;
}

///////////////////////////////////////////////////////////////
// Dispatcher methods

void Runner::Pend(uint8_t action, Input input, fit::function<void(zx_status_t)> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!idle_) {
    FX_LOGS(WARNING) << "Attempted to perform action when fuzzing already in progress.";
    callback(ZX_ERR_BAD_STATE);
    return;
  }
  idle_ = false;
  action_ = action;
  input_ = std::move(input);
  callback_ = std::move(callback);
  sync_completion_signal(&worker_sync_);
}

void Runner::Execute(Input input, fit::function<void(zx_status_t)> callback) {
  Pend(kExecute, std::move(input), std::move(callback));
}

void Runner::Minimize(Input input, fit::function<void(zx_status_t)> callback) {
  Pend(kMinimize, std::move(input), std::move(callback));
}

void Runner::Cleanse(Input input, fit::function<void(zx_status_t)> callback) {
  Pend(kCleanse, std::move(input), std::move(callback));
}

void Runner::Fuzz(fit::function<void(zx_status_t)> callback) {
  Input input;
  Pend(kFuzz, std::move(input), std::move(callback));
}

void Runner::Merge(fit::function<void(zx_status_t)> callback) {
  Input input;
  Pend(kMerge, std::move(input), std::move(callback));
}

void Runner::Worker() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      idle_ = true;
    }
    // Wait indefinitely. Destroying this object will send |kStop|.
    sync_completion_wait(&worker_sync_, ZX_TIME_INFINITE);
    sync_completion_reset(&worker_sync_);
    uint8_t action;
    Input input;
    fit::function<void(zx_status_t)> callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      action = action_;
      input = std::move(input_);
      callback = std::move(callback_);
    }
    if (action == kStop) {
      return;
    }
    zx_status_t status = ZX_OK;
    switch (action) {
      case kExecute:
        status = SyncExecute(input);
        break;
      case kMinimize:
        status = SyncMinimize(input);
        break;
      case kCleanse:
        status = SyncCleanse(input);
        break;
      case kFuzz:
        status = SyncFuzz();
        break;
      case kMerge:
        status = SyncMerge();
        break;
      default:
        FX_NOTREACHED();
    }
    callback(status);
  }
}

///////////////////////////////////////////////////////////////
// Status-related methods.

void Runner::AddMonitor(MonitorPtr monitor) {
  std::lock_guard<std::mutex> lock(mutex_);
  monitors_.push_back(std::move(monitor));
}

Status Runner::CollectStatus() {
  std::lock_guard<std::mutex> lock(mutex_);
  return CollectStatusLocked();
}

void Runner::UpdateMonitors(UpdateReason reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto status = CollectStatusLocked();
  for (auto& monitor : monitors_) {
    auto copy = CopyStatus(status);
    monitor->Update(reason, std::move(copy), []() {});
  }
  if (reason == UpdateReason::DONE) {
    monitors_.clear();
  }
}

}  // namespace fuzzing
