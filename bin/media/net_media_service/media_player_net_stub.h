// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <endian.h>
#include <zx/channel.h>

#include "garnet/bin/media/net_media_service/media_player_messages.h"
#include "lib/fxl/macros.h"
#include <fuchsia/cpp/media.h>
#include "lib/netconnector/cpp/message_relay.h"
#include "lib/netconnector/cpp/net_stub_responder.h"

namespace media {

// Controls a media player on behalf of a remote party.
class MediaPlayerNetStub
    : public std::enable_shared_from_this<MediaPlayerNetStub> {
 public:
  MediaPlayerNetStub(
      MediaPlayer* player,
      zx::channel channel,
      netconnector::NetStubResponder<MediaPlayer, MediaPlayerNetStub>*
          responder);

  ~MediaPlayerNetStub();

 private:
  // Handles a message received via the relay.
  void HandleReceivedMessage(std::vector<uint8_t> message);

  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(uint64_t version = MediaPlayer::kInitialStatus,
                           MediaPlayerStatusPtr status = nullptr);

  MediaPlayer* player_;
  netconnector::MessageRelay message_relay_;
  netconnector::NetStubResponder<MediaPlayer, MediaPlayerNetStub>* responder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetStub);
};

}  // namespace media
