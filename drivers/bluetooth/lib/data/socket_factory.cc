// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_factory.h"

#include <zircon/assert.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

#include "socket_channel_relay.h"

namespace btlib {
namespace data {

SocketFactory::SocketFactory() : weak_ptr_factory_(this) {}

SocketFactory::~SocketFactory() {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
}

zx::socket SocketFactory::MakeSocketForChannel(
    fbl::RefPtr<l2cap::Channel> channel) {
  ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
  ZX_DEBUG_ASSERT(channel);

  if (channel_to_relay_.find(channel->unique_id()) != channel_to_relay_.end()) {
    bt_log(ERROR, "l2cap", "channel %u @ %u is already bound to a socket",
           channel->link_handle(), channel->id());
    return {};
  }

  zx::socket local_socket, remote_socket;
  const auto status =
      zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  if (status != ZX_OK) {
    bt_log(ERROR, "l2cap", "Failed to create socket for channel %u @ %u: %s",
           channel->link_handle(), channel->id(), zx_status_get_string(status));
    return {};
  }

  auto relay = std::make_unique<internal::SocketChannelRelay>(
      std::move(local_socket), channel,
      internal::SocketChannelRelay::DeactivationCallback(
          [self = weak_ptr_factory_.GetWeakPtr()](
              l2cap::Channel::UniqueId unique_id) mutable {
            ZX_DEBUG_ASSERT_MSG(self, "(unique_id=%lu)", unique_id);
            size_t n_erased = self->channel_to_relay_.erase(unique_id);
            ZX_DEBUG_ASSERT_MSG(n_erased == 1, "(n_erased=%zu, unique_id=%lu)",
                                n_erased, unique_id);
          }));

  // Note: Activate() may abort, if |channel| has been Activated() without going
  // through this SocketFactory.
  if (!relay->Activate()) {
    bt_log(ERROR, "l2cap", "Failed to Activate() relay for channel %u",
           channel->id());
    return {};
  }

  channel_to_relay_.emplace(channel->unique_id(), std::move(relay));
  return remote_socket;
}

}  // namespace data
}  // namespace btlib
