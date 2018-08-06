// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_factory.h"

#include <zircon/status.h>

#include "lib/fxl/logging.h"

#include "garnet/drivers/bluetooth/lib/l2cap/socket_channel_relay.h"

namespace btlib {
namespace l2cap {

SocketFactory::SocketFactory() : weak_ptr_factory_(this) {}

SocketFactory::~SocketFactory() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
}

zx::socket SocketFactory::MakeSocketForChannel(fbl::RefPtr<Channel> channel) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(channel);

  if (channel_to_relay_.find(channel->id()) != channel_to_relay_.end()) {
    FXL_LOG(ERROR) << "l2cap: " << __func__ << ": channel " << channel->id()
                   << " is already bound to a socket.";
    return {};
  }

  zx::socket local_socket, remote_socket;
  const auto status =
      zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "l2cap: Failed to create socket for channel "
                   << channel->id() << ": " << zx_status_get_string(status);
    return {};
  }

  auto relay = std::make_unique<internal::SocketChannelRelay>(
      std::move(local_socket), channel,
      internal::SocketChannelRelay::DeactivationCallback(
          [self =
               weak_ptr_factory_.GetWeakPtr()](ChannelId channel_id) mutable {
            FXL_DCHECK(self) << "(channel_id=" << channel_id << ")";
            auto n_erased = self->channel_to_relay_.erase(channel_id);
            FXL_DCHECK(n_erased == 1) << "(n_erased=" << n_erased
                                      << ", channel_id=" << channel_id << ")";
          }));

  // Note: Activate() may abort, if |channel| has been Activated() without going
  // through this SocketFactory.
  if (!relay->Activate()) {
    FXL_LOG(ERROR) << "l2cap: Failed to Activate() relay for channel "
                   << channel->id();
    return {};
  }

  channel_to_relay_[channel->id()] = std::move(relay);
  return remote_socket;
}

}  // namespace l2cap
}  // namespace btlib
