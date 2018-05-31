// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_PROXY_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_PROXY_H_

#include <memory>
#include <string>

#include "garnet/bin/media/net_media_service/media_player_messages.h"
#include "garnet/bin/media/net_media_service/net_media_service_impl.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace media_player {

// Proxy that allows a client to control a remote media player.
class MediaPlayerNetProxy
    : public NetMediaServiceImpl::MultiClientProduct<MediaPlayer>,
      public MediaPlayer {
 public:
  static std::shared_ptr<MediaPlayerNetProxy> Create(
      fidl::StringPtr device_name, fidl::StringPtr service_name,
      fidl::InterfaceRequest<MediaPlayer> request, NetMediaServiceImpl* owner);

  ~MediaPlayerNetProxy() override;

  // MediaPlayer implementation.
  void SetHttpSource(fidl::StringPtr http_url) override;

  void SetFileSource(zx::channel file_channel) override;

  void SetReaderSource(
      fidl::InterfaceHandle<SeekingReader> reader_handle) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void SetGain(float gain) override;

  void CreateView(fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewManager> view_manager,
                  fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                      view_owner_request) override;

  void SetAudioRenderer(
      fidl::InterfaceHandle<media::AudioRenderer2> audio_renderer) override;

  void AddBinding(fidl::InterfaceRequest<MediaPlayer> request) override;

 private:
  MediaPlayerNetProxy(fidl::StringPtr device_name, fidl::StringPtr service_name,
                      fidl::InterfaceRequest<MediaPlayer> request,
                      NetMediaServiceImpl* owner);

  void SendTimeCheckMessage();

  void HandleReceivedMessage(std::vector<uint8_t> message);

  void SendStatusUpdates();

  netconnector::MessageRelay message_relay_;
  MediaPlayerStatusPtr status_;
  media::TimelineFunction remote_to_local_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetProxy);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_NET_PROXY_H_
