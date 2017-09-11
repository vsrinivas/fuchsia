// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/waiter/default.h"

#include "lib/fxl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace {

class HandleWatcher : public MessageLoopHandler {
 public:
  HandleWatcher(mx_handle_t handle,
                FidlAsyncWaitCallback callback,
                void* context)
      : key_(0), handle_(handle), callback_(callback), context_(context) {}

  ~HandleWatcher() {
    if (key_)
      MessageLoop::GetCurrent()->RemoveHandler(key_);
  }

  void Start(mx_signals_t signals, mx_time_t timeout) {
    MessageLoop* message_loop = MessageLoop::GetCurrent();
    FXL_DCHECK(message_loop) << "DefaultAsyncWaiter requires a MessageLoop";
    fxl::TimeDelta timeout_delta;
    if (timeout == MX_TIME_INFINITE)
      timeout_delta = fxl::TimeDelta::Max();
    else
      timeout_delta = fxl::TimeDelta::FromNanoseconds(timeout);
    key_ = message_loop->AddHandler(this, handle_, signals, timeout_delta);
  }

 protected:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(MX_OK, pending, count);
  }

  void OnHandleError(mx_handle_t handle, mx_status_t status) override {
    FXL_DCHECK(handle_ == handle);
    CallCallback(status, MX_SIGNAL_NONE, 0);
  }

 private:
  void CallCallback(mx_status_t status, mx_signals_t pending, uint64_t count) {
    FidlAsyncWaitCallback callback = callback_;
    void* context = context_;
    delete this;
    callback(status, pending, count, context);
  }

  MessageLoop::HandlerKey key_;
  mx_handle_t handle_;
  FidlAsyncWaitCallback callback_;
  void* context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};

FidlAsyncWaitID AsyncWait(mx_handle_t handle,
                          mx_signals_t signals,
                          mx_time_t timeout,
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
}  // namespace mtl

namespace fidl {

FXL_EXPORT const FidlAsyncWaiter* GetDefaultAsyncWaiter() {
  return &mtl::kDefaultAsyncWaiter;
}

}  // namespace fidl
