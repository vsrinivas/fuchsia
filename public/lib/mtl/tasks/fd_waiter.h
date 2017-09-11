// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_FD_WAITER_H_
#define LIB_MTL_TASKS_FD_WAITER_H_

#include <magenta/types.h>
#include <mxio/private.h>

#include <functional>

#include "lib/fxl/fxl_export.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace mtl {

class FXL_EXPORT FDWaiter : public MessageLoopHandler {
 public:
  FDWaiter();
  ~FDWaiter() override;

  // If the wait was successful, the first argument will be MX_OK and the
  // second argument will be the pending events on the file descriptor. If the
  // wait failed (e.g., because the file descriptor was closed during the wait),
  // the first argument will be the error code and the second argument will be
  // zero.
  using Callback = std::function<void(mx_status_t, uint32_t)>;

  // Creates an asynchronous, one-shot wait for the given events on the given
  // file descriptor until the given timeout. Calls |callback| when the wait
  // completes. (See |Callback| for a description of the arguments passed to the
  // callback.)
  //
  // Only one wait can be outstanding at a time. Calling wait while a wait is
  // still underway is an error.
  //
  // * |callback| is the callback to call when the wait is complete.
  // * |fd| is the file descriptor to wait on.
  // * |events| is a bitmask of POSIX-style events (EPOLLIN, EPOLLOUT,
  //   EPOLLERR).
  // * |timeout| is a time limit for the wait.
  //
  // Returns true if |fd| is a valid file descriptor that supports waiting on
  // the given events. Otherwise, returns false.
  bool Wait(Callback callback,
            int fd,
            uint32_t events,
            fxl::TimeDelta timeout = fxl::TimeDelta::Max());

  // Cancels an outstanding wait.
  //
  // It is an error to call cancel if there is no outstanding wait.
  void Cancel();

 private:
  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  mxio_t* io_;
  MessageLoop::HandlerKey key_;
  Callback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FDWaiter);
};

}  // namespace mtl

#endif  // LIB_MTL_TASKS_FD_WAITER_H_
