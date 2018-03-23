// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "garnet/bin/media/net_media_service/media_player_messages.h"
#include "garnet/bin/media/net_media_service/net_media_service_impl.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace media {

// Proxy that allows a client to control a remote media player.
class MediaPlayerNetProxy
    : public NetMediaServiceImpl::MultiClientProduct<MediaPlayer>,
      public MediaPlayer {
 public:
  static std::shared_ptr<MediaPlayerNetProxy> Create(
      const f1dl::StringPtr& device_name,
      const f1dl::StringPtr& service_name,
      f1dl::InterfaceRequest<MediaPlayer> request,
      NetMediaServiceImpl* owner);

  ~MediaPlayerNetProxy() override;

  // MediaPlayer implementation.
  void SetHttpSource(const f1dl::StringPtr& http_url) override;

  void SetFileSource(zx::channel file_channel) override;

  void SetReaderSource(
      f1dl::InterfaceHandle<SeekingReader> reader_handle) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void SetGain(float gain) override;

  void CreateView(
      f1dl::InterfaceHandle<views_v1::ViewManager> view_manager,
      f1dl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request) override;

  void SetAudioRenderer(
      f1dl::InterfaceHandle<AudioRenderer> audio_renderer,
      f1dl::InterfaceHandle<MediaRenderer> media_renderer) override;

  void AddBinding(f1dl::InterfaceRequest<MediaPlayer> request) override;

 private:
  MediaPlayerNetProxy(const f1dl::StringPtr& device_name,
                      const f1dl::StringPtr& service_name,
                      f1dl::InterfaceRequest<MediaPlayer> request,
                      NetMediaServiceImpl* owner);

  void SendTimeCheckMessage();

  void HandleReceivedMessage(std::vector<uint8_t> message);

  netconnector::MessageRelay message_relay_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  MediaPlayerStatusPtr status_;
  TimelineFunction remote_to_local_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetProxy);
};

}  // namespace media
