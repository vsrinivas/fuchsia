// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_TASKS_MESSAGE_LOOP_HANDLER_H_
#define LIB_FSL_TASKS_MESSAGE_LOOP_HANDLER_H_

#include <zircon/types.h>

#include "lib/fxl/fxl_export.h"

namespace fsl {

//
// This class is DEPRECATED.
//
// Please use |async::AutoWait| or |async::Wait| instead.
//
// EXAMPLE:
//
// #include <async/auto_wait.h>
// #include <fbl/function.h>
// #include <zx/channel.h>
//
// class MyWaiter {
// public:
//   MyWaiter(zx::channel channel) :
//       channel_(std::move(channel)),
//       wait_(fsl::MessageLoop::GetCurrent()->async(),
//             channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {
//     wait_.set_handler(fbl::BindMember(this, &MyWaiter::Handler));
//     zx_status_t status = wait_.Begin();
//     if (status != ZX_OK) { /* handle the error */ }
//   }
//
// private:
//   async_wait_result_t Handle(async_t* async, zx_status_t status,
//                              const zx_packet_signal* signal) {
//     if (status == ZX_OK && (signal->observed & ZX_CHANNEL_READABLE)) {
//       /* do something */
//       return ASYNC_WAIT_AGAIN;
//     }
//
//     /* do something else */
//     return ASYNC_WAIT_FINISHED;
//   }
//
//   zx::channel channel_;
//   async::AutoWait wait_;
// };
//
class FXL_EXPORT MessageLoopHandler {
 public:
  // Called when the handle receives signals that the handler was waiting for.
  //
  // The handler remains in the message loop; it will continue to receive
  // signals until it is removed.
  //
  // The |pending| signals represents a snapshot of the signal state when
  // the wait completed; it will always include at least one of the signals
  // that the handler was waiting for but may also include other signals
  // that happen to also be asserted at the same time.
  //
  // The number of pending operations is passed in |count|. This is the count
  // returned by |zx_port_wait|, see:
  // https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/port_wait.md
  // For channels this is the number of messages that are ready to be read.
  //
  // The default implementation does nothing.
  virtual void OnHandleReady(zx_handle_t handle,
                             zx_signals_t pending,
                             uint64_t count);

  // Called when an error occurs while waiting on the handle.
  //
  // The handler is automatically removed by the message loop before invoking
  // this callback; it will not receive any further signals unless it is
  // re-added later.
  //
  // Possible errors are:
  //   - |ZX_ERR_TIMED_OUT|: the handler deadline elapsed
  //   - |ZX_ERR_CANCELED|:  the message loop itself has been destroyed
  //                         rendering it impossible to continue waiting on any
  //                         handles
  //
  // The default implementation does nothing.
  virtual void OnHandleError(zx_handle_t handle, zx_status_t error);

 protected:
  virtual ~MessageLoopHandler();
};

}  // namespace fsl

#endif  // LIB_FSL_TASKS_MESSAGE_LOOP_HANDLER_H_
