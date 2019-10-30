// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_EVENT_HANDLER_H_
#define SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_EVENT_HANDLER_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <memory>

namespace devmgr {

// Mixin for representing a type that represents an RPC handler and is owned
// by an async loop.  The loop will own both the wrapped type and the RPC
// connection handle.
//
// Deriving classes should define and implement this function:
// static void HandleEvent(std::unique_ptr<T> event,
//                         async_dispatcher_t* dispatcher, async::WaitBase* wait,
//                         zx_status_t status, const zx_packet_signal_t* signal);
template <typename T>
class AsyncLoopOwnedEventHandler {
 public:
  explicit AsyncLoopOwnedEventHandler(zx::event event)
      : wait_(this, event.release(), ZX_USER_SIGNAL_0) {}

  ~AsyncLoopOwnedEventHandler() {
    zx_status_t status = wait_.Cancel();
    ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);

    zx_handle_close(wait_.object());
  }

  std::unique_ptr<T> Cancel() {
    ZX_ASSERT(wait_.Cancel() == ZX_OK);
    return std::unique_ptr<T>(static_cast<T*>(this));
  }

  // Begins waiting in |dispatcher| on |event->wait|.  This transfers ownership
  // of |event| to the dispatcher.  The dispatcher returns ownership when the
  // handler is invoked.
  static zx_status_t BeginWait(std::unique_ptr<T> event, async_dispatcher_t* dispatcher) {
    zx_status_t status = event->wait_.Begin(dispatcher);
    if (status == ZX_OK) {
      __UNUSED auto ptr = event.release();
    }
    return status;
  }

  // Entrypoint for the event handler that captures the pointer ownership
  // semantics.
  void HandleEvent(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal) {
    std::unique_ptr<T> self(static_cast<T*>(this));
    T::HandleEvent(std::move(self), dispatcher, wait, status, signal);
  }

  zx::unowned_event event() const { return zx::unowned_event(wait_.object()); }

  // Sets the event to the given handle and returns the old value
  zx::event set_event(zx::event h) {
    zx::event old(wait_.object());
    wait_.set_object(h.release());
    return old;
  }

  using WaitType =
      async::WaitMethod<AsyncLoopOwnedEventHandler<T>, &AsyncLoopOwnedEventHandler<T>::HandleEvent>;

 private:
  WaitType wait_{this, ZX_HANDLE_INVALID, ZX_USER_SIGNAL_0};
};

}  // namespace devmgr

#endif  // SRC_DEVICES_HOST_ASYNC_LOOP_OWNED_EVENT_HANDLER_H_
