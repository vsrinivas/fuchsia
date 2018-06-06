// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_STUB_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_STUB_H_

#include <memory>

#include <endian.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/channel.h>

#include "garnet/bin/media/net_media_service/media_player_messages.h"
#include "lib/fxl/macros.h"
#include "lib/netconnector/cpp/message_relay.h"
#include "lib/netconnector/cpp/net_stub_responder.h"

namespace media_player {

// Controls a media player on behalf of a remote party.
class MediaPlayerNetStub
    : public std::enable_shared_from_this<MediaPlayerNetStub> {
 public:
  MediaPlayerNetStub(
      const fidl::InterfacePtr<fuchsia::mediaplayer::MediaPlayer>& player,
      zx::channel channel,
      netconnector::NetStubResponder<fuchsia::mediaplayer::MediaPlayer,
                                     MediaPlayerNetStub>* responder);

  ~MediaPlayerNetStub();

 private:
  // Handles a message received via the relay.
  void HandleReceivedMessage(std::vector<uint8_t> message);

  // Handles a status change from the player.
  void HandleStatusChanged(
      const fuchsia::mediaplayer::MediaPlayerStatus& status);

  const fidl::InterfacePtr<fuchsia::mediaplayer::MediaPlayer>& player_;
  netconnector::MessageRelay message_relay_;
  netconnector::NetStubResponder<fuchsia::mediaplayer::MediaPlayer,
                                 MediaPlayerNetStub>* responder_;
  fuchsia::mediaplayer::MediaPlayerStatusPtr cached_status_;
  bool time_check_received_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetStub);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_STUB_H_
