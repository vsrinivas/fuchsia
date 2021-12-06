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
  kIdle,
  kStop,
};

}  // namespace

Runner::Runner()
    : action_(kIdle),
      close_([this]() { CloseImpl(); }),
      interrupt_([this] { InterruptImpl(); }),
      join_([this]() { JoinImpl(); }) {
  // Start the worker and ensure is up and running.
  worker_ = std::thread([this]() { Worker(); });
  bool idle = true;
  do {
    std::this_thread::yield();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      idle = idle_;
    }
  } while (!idle);
}

Runner::~Runner() {
  close_.Run();
  interrupt_.Run();
  join_.Run();
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
// Worker methods

void Runner::Pend(uint8_t action, Input input, fit::function<void(zx_status_t)> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (action_ == kStop) {
    FX_LOGS(WARNING) << "Attempted to perform action when engine is stopping.";
    callback(ZX_ERR_BAD_STATE);
    return;
  }
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
    // Wait indefinitely. Destroying this object will call |StopImpl|.
    sync_completion_wait(&worker_sync_, ZX_TIME_INFINITE);
    sync_completion_reset(&worker_sync_);
    uint8_t action;
    Input input;
    fit::function<void(zx_status_t)> callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (action_ == kStop) {
        return;
      }
      action = action_;
      input = std::move(input_);
      callback = std::move(callback_);
    }
    zx_status_t status = ZX_OK;
    ClearErrors();
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

void Runner::AddMonitor(fidl::InterfaceHandle<Monitor> monitor) {
  monitors_.Add(std::move(monitor));
}

void Runner::UpdateMonitors(UpdateReason reason) {
  monitors_.SetStatus(CollectStatus());
  monitors_.Update(reason);
}

void Runner::ClearErrors() {
  result_ = Result::NO_ERRORS;
  result_input_.Clear();
}

///////////////////////////////////////////////////////////////
// Stop-related methods.

void Runner::CloseImpl() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_ = false;
    action_ = kStop;
    sync_completion_signal(&worker_sync_);
  }
}

void Runner::InterruptImpl() {
  // no-op in the base class.
}

void Runner::JoinImpl() {
  FX_DCHECK(worker_.joinable());
  worker_.join();
}

}  // namespace fuzzing
