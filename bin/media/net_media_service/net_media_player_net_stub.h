// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <endian.h>
#include <mx/channel.h>

#include "lib/media/fidl/net_media_player.fidl.h"
#include "garnet/bin/media/net_media_service/net_media_player_messages.h"
#include "lib/ftl/macros.h"
#include "lib/netconnector/cpp/message_relay.h"
#include "lib/netconnector/cpp/net_stub_responder.h"

namespace media {

// Controls a media player on behalf of a remote party.
class NetMediaPlayerNetStub
    : public std::enable_shared_from_this<NetMediaPlayerNetStub> {
 public:
  NetMediaPlayerNetStub(
      NetMediaPlayer* player,
      mx::channel channel,
      netconnector::NetStubResponder<NetMediaPlayer, NetMediaPlayerNetStub>*
          responder);

  ~NetMediaPlayerNetStub();

 private:
  // Handles a message received via the relay.
  void HandleReceivedMessage(std::vector<uint8_t> message);

  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(uint64_t version = NetMediaPlayer::kInitialStatus,
                           MediaPlayerStatusPtr status = nullptr);

  NetMediaPlayer* player_;
  netconnector::MessageRelay message_relay_;
  netconnector::NetStubResponder<NetMediaPlayer, NetMediaPlayerNetStub>*
      responder_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetMediaPlayerNetStub);
};

}  // namespace media
