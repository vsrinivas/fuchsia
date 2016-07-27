// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/environment/default_async_waiter.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/c/environment/async_waiter.h"

namespace mtl {
namespace internal {
namespace {

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
    key_ = message_loop->AddHandler(this, handle_, signals, timeout);
  }

 protected:
  void OnHandleReady(MojoHandle handle) override {
    FTL_DCHECK(handle_ == handle);
    CallCallback(MOJO_RESULT_OK);
  }

  void OnHandleError(MojoHandle handle, MojoResult result) override {
    FTL_DCHECK(handle_ == handle);
    CallCallback(result);
  }

 private:
  void CallCallback(MojoResult result) {
    MojoAsyncWaitCallback callback = callback_;
    void* context = context_;
    delete this;
    callback(context, result);
  }

  MessageLoop::HandlerKey key_;
  MojoHandle handle_;
  MojoAsyncWaitCallback callback_;
  void* context_;

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
