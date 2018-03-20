// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/net_media_service/media_player_net_stub.h"
#include "garnet/bin/media/net_media_service/net_media_service_impl.h"
#include "lib/fxl/macros.h"
#include "lib/media/fidl/media_player.fidl.h"
#include "lib/netconnector/cpp/net_stub_responder.h"

namespace media {

// Publishes a MediaPlayer.
//
// Most of the work is done by NetStubResponder. This class just controls the
// lifetime of the responder and keeps a reference to the published MediaPlayer.
class MediaPlayerNetPublisher : public NetMediaServiceImpl::ProductBase {
 public:
  static std::shared_ptr<MediaPlayerNetPublisher> Create(
      const f1dl::StringPtr& service_name,
      f1dl::InterfaceHandle<MediaPlayer> media_player,
      NetMediaServiceImpl* owner);

  ~MediaPlayerNetPublisher() override;

 private:
  MediaPlayerNetPublisher(const f1dl::StringPtr& service_name,
                          f1dl::InterfaceHandle<MediaPlayer> media_player,
                          NetMediaServiceImpl* owner);

  MediaPlayerPtr media_player_;
  netconnector::NetStubResponder<MediaPlayer, MediaPlayerNetStub> responder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetPublisher);
};

}  // namespace media
