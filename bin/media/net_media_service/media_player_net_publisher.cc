// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/media_player_net_publisher.h"

#include <fcntl.h>
#include <media/cpp/fidl.h>

#include "lib/fxl/logging.h"
#include "lib/url/gurl.h"

namespace media_player {

// static
std::shared_ptr<MediaPlayerNetPublisher> MediaPlayerNetPublisher::Create(
    fidl::StringPtr service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player,
    NetMediaServiceImpl* owner) {
  return std::shared_ptr<MediaPlayerNetPublisher>(new MediaPlayerNetPublisher(
      service_name, std::move(media_player), owner));
}

MediaPlayerNetPublisher::MediaPlayerNetPublisher(
    fidl::StringPtr service_name,
    fidl::InterfaceHandle<MediaPlayer> media_player,
    NetMediaServiceImpl* owner)
    : NetMediaServiceImpl::ProductBase(owner),
      media_player_(media_player.Bind()),
      responder_(media_player_, service_name, owner->application_context()) {
  FXL_DCHECK(owner);

  media_player_.set_error_handler([this]() {
    media_player_.Unbind();
    ReleaseFromOwner();
  });
}

MediaPlayerNetPublisher::~MediaPlayerNetPublisher() {
  media_player_.Unbind();
}

}  // namespace media_player
