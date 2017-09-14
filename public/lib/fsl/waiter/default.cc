// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/waiter/default.h"

#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace fsl {
namespace {

class HandleWatcher : public MessageLoopHandler {
 public:
  HandleWatcher(zx_handle_t handle,
                FidlAsyncWaitCallback callback,
                void* context)
      : key_(0), handle_(handle), callback_(callback), context_(context) {}

  ~HandleWatcher() {
    if (key_)
      MessageLoop::GetCurrent()->RemoveHandler(key_);
  }

  void Start(zx_signals_t signals, zx_time_t timeout) {
    MessageLoop* message_loop = MessageLoop::GetCurrent();
    FXL_DCHECK(message_loop) << "DefaultAsyncWaiter requires a MessageLoop";
    fxl::TimeDelta timeout_delta;
    if (timeout == ZX_TIME_INFINITE)
      timeout_delta = fxl::TimeDelta::Max();
    else
      timeout_delta = fxl::TimeDelta::FromNanoseconds(timeout);
    key_ = message_loop->AddHandler(this, handle_, signals, timeout_delta);
  }

 protected:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(ZX_OK, pending, count);
  }

  void OnHandleError(zx_handle_t handle, zx_status_t status) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(status, ZX_SIGNAL_NONE, 0);
  }

 private:
  void CallCallback(zx_status_t status, zx_signals_t pending, uint64_t count) {
    FidlAsyncWaitCallback callback = callback_;
    void* context = context_;
    delete this;
    callback(status, pending, count, context);
  }

  MessageLoop::HandlerKey key_;
  zx_handle_t handle_;
  FidlAsyncWaitCallback callback_;
  void* context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};

FidlAsyncWaitID AsyncWait(zx_handle_t handle,
                          zx_signals_t signals,
                          zx_time_t timeout,
                          FidlAsyncWaitCallback callback,
                          void* context) {
  // This instance will be deleted when done or cancelled.
  HandleWatcher* watcher = new HandleWatcher(handle, callback, context);
  watcher->Start(signals, timeout);
  return reinterpret_cast<FidlAsyncWaitID>(watcher);
}

void CancelWait(FidlAsyncWaitID wait_id) {
  delete reinterpret_cast<HandleWatcher*>(wait_id);
}

constexpr FidlAsyncWaiter kDefaultAsyncWaiter = {AsyncWait, CancelWait};

}  // namespace
}  // namespace fsl

namespace fidl {

FXL_EXPORT const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  return &fsl::kDefaultAsyncWaiter;
}

}  // namespace fidl
