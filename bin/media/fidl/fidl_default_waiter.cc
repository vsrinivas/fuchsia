// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_default_waiter.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace media {
namespace {

class HandleWatcher : public fsl::MessageLoopHandler {
 public:
  HandleWatcher(zx_handle_t handle,
                FidlAsyncWaiter::FidlAsyncWaitCallback callback)
      : key_(0), handle_(handle), callback_(callback) {}

  ~HandleWatcher() {
    if (key_)
      fsl::MessageLoop::GetCurrent()->RemoveHandler(key_);
  }

  void Start(zx_signals_t signals, zx_time_t timeout) {
    fsl::MessageLoop* message_loop = fsl::MessageLoop::GetCurrent();
    FXL_DCHECK(message_loop) << "DefaultAsyncWaiter requires a MessageLoop";
    fxl::TimeDelta timeout_delta;
    if (timeout == ZX_TIME_INFINITE)
      timeout_delta = fxl::TimeDelta::Max();
    else
      timeout_delta = fxl::TimeDelta::FromNanoseconds(timeout);
    key_ = message_loop->AddHandler(this, handle_, signals, timeout_delta);
  }

 protected:
  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(ZX_OK, pending, count);
  }

  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(status, ZX_SIGNAL_NONE, 0);
  }

 private:
  void CallCallback(zx_status_t status, zx_signals_t pending, uint64_t count) {
    FidlAsyncWaiter::FidlAsyncWaitCallback callback = callback_;
    delete this;
    callback(status, pending, count);
  }

  fsl::MessageLoop::HandlerKey key_;
  zx_handle_t handle_;
  FidlAsyncWaiter::FidlAsyncWaitCallback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};

}  // namespace

FidlAsyncWaitID FidlAsyncWaiter::AsyncWait(
    zx_handle_t handle,
    zx_signals_t signals,
    zx_time_t timeout,
    FidlAsyncWaitCallback callback) const {
  // This instance will be deleted when done or cancelled.
  HandleWatcher* watcher = new HandleWatcher(handle, callback);
  watcher->Start(signals, timeout);
  return reinterpret_cast<FidlAsyncWaitID>(watcher);
}

void FidlAsyncWaiter::CancelWait(FidlAsyncWaitID wait_id) const {
  delete reinterpret_cast<HandleWatcher*>(wait_id);
}

const FidlAsyncWaiter kDefaultAsyncWaiter;

const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  return &kDefaultAsyncWaiter;
}

}  // namespace media
