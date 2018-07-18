// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_H_

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
class Frame;

class Channel : public fbl::RefCounted<Channel> {
 public:
  virtual ~Channel() = default;

  using RxCallback = fit::function<void(common::ByteBufferPtr)>;
  using ClosedCallback = fit::closure;
  virtual void Activate(RxCallback rx_callback, ClosedCallback closed_callback,
                        async_dispatcher_t* dispatcher) = 0;

  // Send a buffer of user data. Takes ownership of |data|. This method is
  // asynchronous, and there is no notification of delivery. We operate under
  // the assumption that the underlying transport is reliable. The channel must
  // be activated prior to sending.
  virtual void Send(common::ByteBufferPtr data) = 0;

 protected:
  friend class rfcomm::Session;

  Channel(DLCI dlci, Session* session);

  RxCallback rx_callback_;
  ClosedCallback closed_callback_;
  async_dispatcher_t* dispatcher_;

  const DLCI dlci_;
  // The Session owning this Channel. |session_| will always outlive |this|.
  Session* session_;

  // True if the channel is established (DLC Establishment has taken place)
  bool established_;

  // The negotiation state of this channel
  ParameterNegotiationState negotiation_state_;

  // The number of local and remote credits available on this channel.
  Credits local_credits_;
  Credits remote_credits_;

  // Frames waiting on this channel to receive credits to be sent (and
  // sent callbacks)
  std::queue<std::pair<std::unique_ptr<Frame>, fit::closure>> wait_queue_;

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
  friend class rfcomm::Session;

  // This should only be called from Session.
  ChannelImpl(DLCI dlci, Session* session);

  // This should only be called from Session.
  void Receive(std::unique_ptr<common::ByteBuffer> data) override;

  std::queue<std::unique_ptr<common::ByteBuffer>> pending_rxed_frames_;
};

}  //  namespace internal

}  // namespace rfcomm
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_RFCOMM_CHANNEL_H_
