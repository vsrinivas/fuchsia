// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/environment/default_async_waiter.h"

#include <mojo/environment/async_waiter.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace internal {
namespace {

MojoResult ToMojoResult(mx_status_t status) {
  switch (status) {
    case NO_ERROR:
      return MOJO_RESULT_OK;
    case ERR_HANDLE_CLOSED:
      return MOJO_SYSTEM_RESULT_CANCELLED;
    case ERR_NO_MEMORY:
      return MOJO_SYSTEM_RESULT_RESOURCE_EXHAUSTED;
    case ERR_BAD_HANDLE:
    case ERR_INVALID_ARGS:
      return MOJO_SYSTEM_RESULT_INVALID_ARGUMENT;
    case ERR_TIMED_OUT:
      return MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED;
    case ERR_BAD_STATE:
      return MOJO_SYSTEM_RESULT_FAILED_PRECONDITION;
    // TODO(vtl): The Mojo version doesn't require any rights to wait, whereas
    // Magenta requires MX_RIGHT_READ.
    case ERR_ACCESS_DENIED:
      return MOJO_SYSTEM_RESULT_PERMISSION_DENIED;
    default:
      return MOJO_SYSTEM_RESULT_UNKNOWN;
  }
}

class HandleWatcher : public MessageLoopHandler {
 public:
  HandleWatcher(MojoHandle handle,
                MojoAsyncWaitCallback callback,
                void* context)
      : key_(0), handle_(handle), callback_(callback), context_(context) {}

  ~HandleWatcher() {
    if (key_)
      MessageLoop::GetCurrent()->RemoveHandler(key_);
  }

  void Start(MojoHandleSignals signals, MojoDeadline deadline) {
    MessageLoop* message_loop = MessageLoop::GetCurrent();
    FTL_DCHECK(message_loop) << "DefaultAsyncWaiter requires a MessageLoop";
    ftl::TimeDelta timeout;
    if (deadline == MOJO_DEADLINE_INDEFINITE)
      timeout = ftl::TimeDelta::Max();
    else
      timeout = ftl::TimeDelta::FromMicroseconds(deadline);
    awaited_signals_ = static_cast<mx_signals_t>(signals);
    // Emulate Mojo's behavior of implicitly waiting for peer closed on
    // channels or data pipes.
    mx_status_t inferred_signals = awaited_signals_;
    if (inferred_signals & (MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE))
      inferred_signals |= MX_SIGNAL_PEER_CLOSED;
    key_ = message_loop->AddHandler(this, static_cast<mx_handle_t>(handle_),
                                    inferred_signals, timeout);
  }

 protected:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override {
    FTL_DCHECK(handle_ == static_cast<MojoHandle>(handle));
    // Emulate Mojo's behavior of reporting ERR_BAD_STATE for unsatisfiable
    // signals.
    if (!(pending & awaited_signals_)) {
      CallCallback(ERR_BAD_STATE);
    } else {
      CallCallback(NO_ERROR);
    }
  }

  void OnHandleError(mx_handle_t handle, mx_status_t status) override {
    FTL_DCHECK(handle_ == static_cast<MojoHandle>(handle));
    CallCallback(status);
  }

 private:
  void CallCallback(mx_status_t status) {
    MojoAsyncWaitCallback callback = callback_;
    void* context = context_;
    delete this;
    callback(context, ToMojoResult(status));
  }

  MessageLoop::HandlerKey key_;
  MojoHandle handle_;
  MojoAsyncWaitCallback callback_;
  void* context_;
  mx_signals_t awaited_signals_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};

MojoAsyncWaitID AsyncWait(MojoHandle handle,
                          MojoHandleSignals signals,
                          MojoDeadline deadline,
                          MojoAsyncWaitCallback callback,
                          void* context) {
  // This instance will be deleted when done or cancelled.
  HandleWatcher* watcher = new HandleWatcher(handle, callback, context);
  watcher->Start(signals, deadline);
  return reinterpret_cast<MojoAsyncWaitID>(watcher);
}

void CancelWait(MojoAsyncWaitID wait_id) {
  delete reinterpret_cast<HandleWatcher*>(wait_id);
}

}  // namespace

constexpr MojoAsyncWaiter kDefaultAsyncWaiter = {AsyncWait, CancelWait};

}  // namespace internal
}  // namespace mtl
