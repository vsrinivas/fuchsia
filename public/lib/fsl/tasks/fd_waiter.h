// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_TASKS_FD_WAITER_H_
#define LIB_FSL_TASKS_FD_WAITER_H_

#include <functional>

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/private.h>
#include <zircon/types.h>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"

namespace fsl {

class FXL_EXPORT FDWaiter {
 public:
  FDWaiter(async_dispatcher_t* dispatcher = async_get_default_dispatcher());
  ~FDWaiter();

  // If the wait was successful, the first argument will be ZX_OK and the
  // second argument will be the pending events on the file descriptor. If the
  // wait failed (e.g., because the file descriptor was closed during the wait),
  // the first argument will be the error code and the second argument will be
  // zero.
  using Callback = std::function<void(zx_status_t, uint32_t)>;

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
  // * |events| is a bitmask of POSIX-style events (|POLLIN|, |POLLOUT|,
  //   |POLLERR|).
  //
  // Returns true if |fd| is a valid file descriptor that supports waiting on
  // the given events. Otherwise, returns false.
  bool Wait(Callback callback, int fd, uint32_t events);

  // Cancels an outstanding wait.
  //
  // It is an error to call cancel if there is no outstanding wait.
  void Cancel();

 private:
  // Release the fdio_t*
  void Release();

  void Handler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
               zx_status_t status, const zx_packet_signal_t* signal);

  async_dispatcher_t* const dispatcher_;
  fdio_t* io_;
  async::WaitMethod<FDWaiter, &FDWaiter::Handler> wait_{this};
  Callback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FDWaiter);
};

}  // namespace fsl

#endif  // LIB_FSL_TASKS_FD_WAITER_H_
