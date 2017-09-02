// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/src/net_media_service/net_media_player_messages.h"
#include "apps/media/src/net_media_service/net_media_service_impl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace media {

// Proxy that allows a client to control a remote net media player.
class NetMediaPlayerNetProxy
    : public NetMediaServiceImpl::Product<NetMediaPlayer>,
      public NetMediaPlayer {
 public:
  static std::shared_ptr<NetMediaPlayerNetProxy> Create(
      const fidl::String& device_name,
      const fidl::String& service_name,
      fidl::InterfaceRequest<NetMediaPlayer> request,
      NetMediaServiceImpl* owner);

  ~NetMediaPlayerNetProxy() override;

  // NetMediaPlayer implementation.
  void SetUrl(const fidl::String& url) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

 private:
  NetMediaPlayerNetProxy(const fidl::String& device_name,
                         const fidl::String& service_name,
                         fidl::InterfaceRequest<NetMediaPlayer> request,
                         NetMediaServiceImpl* owner);

  void SendTimeCheckMessage();

  void HandleReceivedMessage(std::vector<uint8_t> message);

  netconnector::MessageRelay message_relay_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  MediaPlayerStatusPtr status_;
  TimelineFunction remote_to_local_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetMediaPlayerNetProxy);
};

}  // namespace media
