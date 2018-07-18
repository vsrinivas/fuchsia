// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel_manager.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"

namespace btlib {
namespace rfcomm {

ChannelManager::ChannelManager()
    : dispatcher_(async_get_default_dispatcher()) {}

bool ChannelManager::RegisterL2CAPChannel(
    fbl::RefPtr<l2cap::Channel> l2cap_channel) {
  auto handle = l2cap_channel->link_handle();

  if (handle_to_session_.find(handle) != handle_to_session_.end()) {
    FXL_LOG(WARNING) << "Handle " << handle << " already registered";
    return false;
  }

  auto session = Session::Create(
      l2cap_channel, fit::bind_member(this, &ChannelManager::ChannelOpened),
      dispatcher_);
  if (!session) {
    FXL_LOG(ERROR) << "Couldn't start a session on the given L2CAP channel";
    return false;
  }
  handle_to_session_[handle] = std::move(session);
  return true;
}

void ChannelManager::OpenRemoteChannel(hci::ConnectionHandle handle,
                                       ServerChannel server_channel,
                                       ChannelOpenedCallback channel_opened_cb,
                                       async_dispatcher_t* dispatcher) {
  auto session_it = handle_to_session_.find(handle);

  if (session_it == handle_to_session_.end()) {
    // TODO(gusss): open L2CAP channel if needed.
    FXL_NOTIMPLEMENTED();
    async::PostTask(dispatcher, [cb = std::move(channel_opened_cb)] {
      cb(nullptr, kInvalidServerChannel);
    });
  }

  FXL_DCHECK(session_it != handle_to_session_.end());

  session_it->second->OpenRemoteChannel(
      server_channel, [cb = std::move(channel_opened_cb), dispatcher](
                          auto rfcomm_channel, auto server_channel) mutable {
        async::PostTask(dispatcher, [cb_ = std::move(cb), rfcomm_channel,
                                     server_channel]() {
          cb_(rfcomm_channel, server_channel);
        });
      });
}

ServerChannel ChannelManager::AllocateLocalChannel(
    ChannelOpenedCallback cb, async_dispatcher_t* dispatcher) {
  // Find the first free Server Channel and allocate it.
  for (ServerChannel server_channel = kMinServerChannel;
       server_channel <= kMaxServerChannel; ++server_channel) {
    if (server_channels_.find(server_channel) == server_channels_.end()) {
      server_channels_[server_channel] =
          std::make_pair(std::move(cb), dispatcher);
      return server_channel;
    }
  }

  return kInvalidServerChannel;
}

void ChannelManager::ChannelOpened(fbl::RefPtr<Channel> rfcomm_channel,
                                   ServerChannel server_channel) {
  auto server_channel_it = server_channels_.find(server_channel);
  FXL_DCHECK(server_channel_it != server_channels_.end())
      << "rfcomm: New channel created on an unallocated Server Channel";

  async::PostTask(server_channel_it->second.second,
                  [server_channel, rfcomm_channel,
                   cb = server_channel_it->second.first.share()]() {
                    cb(rfcomm_channel, server_channel);
                  });
}

}  // namespace rfcomm
}  // namespace btlib
