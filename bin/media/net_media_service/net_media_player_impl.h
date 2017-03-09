// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/media_service.fidl.h"
#include "apps/media/services/net_media_player.fidl.h"
#include "apps/media/src/net_media_service/net_media_player_net_stub.h"
#include "apps/media/src/net_media_service/net_media_service_impl.h"
#include "apps/netconnector/lib/net_stub_responder.h"
#include "lib/ftl/macros.h"

namespace media {

// Fidl agent that wraps a MediaPlayer for remote control.
class NetMediaPlayerImpl : public NetMediaServiceImpl::Product<NetMediaPlayer>,
                           public NetMediaPlayer {
 public:
  static std::shared_ptr<NetMediaPlayerImpl> Create(
      const fidl::String& service_name,
      fidl::InterfaceHandle<MediaPlayer> media_player,
      fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
      NetMediaServiceImpl* owner);

  ~NetMediaPlayerImpl() override;

  // NetMediaPlayer implementation.
  void SetUrl(const fidl::String& url) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

 private:
  NetMediaPlayerImpl(
      const fidl::String& service_name,
      fidl::InterfaceHandle<MediaPlayer> media_player,
      fidl::InterfaceRequest<NetMediaPlayer> net_media_player_request,
      NetMediaServiceImpl* owner);

  MediaServicePtr media_service_;
  MediaPlayerPtr media_player_;
  netconnector::NetStubResponder<NetMediaPlayer, NetMediaPlayerNetStub>
      responder_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetMediaPlayerImpl);
};

}  // namespace media
