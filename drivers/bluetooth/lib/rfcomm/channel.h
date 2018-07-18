// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include <fbl/ref_counted.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"
#include "garnet/public/lib/fxl/macros.h"

namespace btlib {
namespace rfcomm {

class Session;

class Channel : public fbl::RefCounted<Channel> {
 public:
  virtual ~Channel() = default;

  using RxCallback = fit::function<void(common::ByteBufferPtr)>;
  using ClosedCallback = fit::closure;
  virtual void Activate(RxCallback rx_callback, ClosedCallback closed_callback,
                        async_dispatcher_t* dispatcher) = 0;

  // Send a buffer of user data. Takes ownership of |data|. This method is
  // asynchronous, and there is no notification of delivery. We operate under
  // the assumption that the underlying transport is reliable.
  virtual void Send(common::ByteBufferPtr data) = 0;

 protected:
  Channel(DLCI dlci, Session* session);

  RxCallback rx_callback_;
  ClosedCallback closed_callback_;
  async_dispatcher_t* dispatcher_;

  const DLCI dlci_;
  // The Session owning this Channel. |session_| will always outlive |this|.
  Session* session_;

  // Called by |session_| when a new frame is received for this channel. If an
  // |rx_callback_| is registered, the frame is forwarded to the callback;
  // otherwise, the frame is buffered and is forwarded once a callback gets
  // registered.
  virtual void Receive(common::ByteBufferPtr data) = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Channel);
};

namespace internal {

class ChannelImpl : public Channel {
 public:
  // Channel overrides
  void Activate(RxCallback rx_callback, ClosedCallback closed_callback,
                async_dispatcher_t* dispatcher) override;
  void Send(common::ByteBufferPtr data) override;

 private:
  friend class Session;

  // This should only be called from Session.
  ChannelImpl();

  // This should only be called from Session.
  void Receive(std::unique_ptr<common::ByteBuffer> data) override;

  // Post a call to |rx_callback_| on |dispatcher_|, passing in |data|.
  void CallRxCallback(std::unique_ptr<common::ByteBuffer> data);

  std::queue<std::unique_ptr<common::ByteBuffer>> pending_rxed_frames_;
};

}  //  namespace internal

}  // namespace rfcomm
}  // namespace btlib
