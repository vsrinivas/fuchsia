// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "apps/media/lib/timeline_function.h"
#include "apps/media/src/net/media_player_net.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "apps/netconnector/lib/message_relay.h"

namespace media {

// Proxy that allows a client to control a remote player.
class MediaPlayerNetProxy : public MediaServiceImpl::Product<MediaPlayer>,
                            public MediaPlayer,
                            public MediaPlayerNet {
 public:
  static std::shared_ptr<MediaPlayerNetProxy> Create(
      std::string device_name,
      std::string service_name,
      fidl::InterfaceRequest<MediaPlayer> request,
      MediaServiceImpl* owner);

  ~MediaPlayerNetProxy() override;

  // MediaPlayer implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Play() override;

  void Pause() override;

  void Seek(int64_t position) override;

 private:
  MediaPlayerNetProxy(std::string device_name,
                      std::string service_name,
                      fidl::InterfaceRequest<MediaPlayer> request,
                      MediaServiceImpl* owner);

  void SendTimeCheckMessage();

  void HandleReceivedMessage(std::vector<uint8_t> message);

  template <typename T>
  T* MessageCast(std::vector<uint8_t>* message) {
    if (message->size() != sizeof(T)) {
      FTL_LOG(ERROR) << "Expected message size " << sizeof(T) << ", got size "
                     << message->size();
      message_relay_.CloseChannel();
      return nullptr;
    }

    T* result = reinterpret_cast<T*>(message->data());
    result->NetToHost();
    return result;
  }

  netconnector::MessageRelay message_relay_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  MediaPlayerStatus status_;
  TimelineFunction remote_to_local_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerNetProxy);
};

}  // namespace media
