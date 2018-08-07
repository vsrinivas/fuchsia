// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SOCKET_CHANNEL_RELAY_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SOCKET_CHANNEL_RELAY_H_

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/zx/socket.h"

#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"

namespace btlib {

namespace l2cap {

namespace internal {

// SocketChannelRelay relays data between a zx::socket and a Channel. This class
// should not be used directly. Instead, see SocketFactory.
//
// THREAD-SAFETY: This class is thread-hostile. Creation, use, and destruction
// _must_ occur on a single thread. |dispatcher|, which _must_ be
// single-threaded, must run on that same thread.
class SocketChannelRelay final {
 public:
  using DeactivationCallback = fit::function<void(ChannelId)>;

  // Creates a SocketChannelRelay which executes on |dispatcher|. Note that
  // |dispatcher| must be single-threaded.
  //
  // The relay works with SocketFactory to manage the relay's lifetime. On any
  // of the "terminal events" (see below), the relay will invoke the
  // DeactivationCallback. On invocation of the DeactivationCallback, the
  // SocketFactory should destroy the relay. The destruction should be done
  // synchronously, as a) destruction must happen on |dispatcher|'s thread, and
  // b) the |dispatcher| may be shutting down.
  //
  // The terminal events are:
  // * the zx::socket is closed
  // * the Channel is closed
  // * the dispatcher begins shutting down
  //
  // Note that requiring |dispatcher| to be single-threaded shouldn't cause
  // increased latency vs. multi-threading, since a) all I/O is non-blocking (so
  // we never leave the thread idle), and b) to provide in-order delivery,
  // moving the data between the zx::socket and the l2cap::Channel needs to be
  // serialized even in the multi-threaded case.
  SocketChannelRelay(zx::socket&& socket, fbl::RefPtr<Channel> channel,
                     DeactivationCallback deactivation_cb);
  ~SocketChannelRelay();

 private:
  enum class RelayState {
    kActivating,
    kActivated,
    kDeactivating,
    kDeactivated,
  };

  // Deactivates and unbinds all callbacks from the zx::socket and the
  // l2cap::Channel. Note that |socket_| closure is left to the dtor.
  void Deactivate();

  RelayState state_;

  const zx::socket socket_;
  const fbl::RefPtr<Channel> channel_;
  async_dispatcher_t* const dispatcher_;
  DeactivationCallback deactivation_cb_;

  const fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketChannelRelay);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SOCKET_CHANNEL_RELAY_H_
