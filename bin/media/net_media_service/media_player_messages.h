// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_MESSAGES_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_MESSAGES_H_

#include <memory>

#include <fuchsia/mediaplayer/cpp/fidl.h>

#include "garnet/bin/media/net_media_service/serialization.h"
#include "lib/fxl/logging.h"

namespace media_player {

// The definitions below are for messages that are serialized and exchanged
// between a media player and a control point. The proxy resides at the
// control point and the stub is adjacent to the media player.

// Types of messages sent by the proxy and handled by the stub.
enum class MediaPlayerInMessageType : uint8_t {
  kTimeCheckRequest,
  kSetHttpSourceRequest,
  kPlayRequest,
  kPauseRequest,
  kSeekRequest,
  kSetGainRequest
};

// Types of messages sent by the stub and handled by the proxy.
enum class MediaPlayerOutMessageType : uint8_t {
  kTimeCheckResponse,
  kStatusNotification
};

// Sent by the proxy to establish a correlation between system times on
// the two systems.
struct MediaPlayerTimeCheckRequest {
  int64_t requestor_time_;  // System time when this message was sent.
};

// Sent by the stub in response to |MediaPlayerTimeCheckRequest| to
// establish a correlation between system times on the two systems.
struct MediaPlayerTimeCheckResponse {
  int64_t requestor_time_;  // From the request
  int64_t responder_time_;  // System time when this message was sent.
};

// Sent by the proxy to request a url change.
struct MediaPlayerSetHttpSourceRequest {
  fidl::StringPtr url_;
};

// Play and Pause have no parameters, so there is no MediaPlayerPlayRequest
// or MediaPlayerPauseRequest.

// Sent by the proxy to request a seek.
struct MediaPlayerSeekRequest {
  int64_t position_;
};

// Sent by the proxy to request a gain change.
struct MediaPlayerSetGainRequest {
  float gain_;
};

// Sent by the stub to notify the proxy of a change in status.
struct MediaPlayerStatusNotification {
  fuchsia::mediaplayer::MediaPlayerStatusPtr status_;
};

// Union-like of all possible messages sent by the proxy and handled
// by the stub.
struct MediaPlayerInMessage {
  static std::unique_ptr<MediaPlayerInMessage> TimeCheckRequest(
      int64_t requestor_time);
  static std::unique_ptr<MediaPlayerInMessage> SetHttpSourceRequest(
      fidl::StringPtr url);
  static std::unique_ptr<MediaPlayerInMessage> PlayRequest();
  static std::unique_ptr<MediaPlayerInMessage> PauseRequest();
  static std::unique_ptr<MediaPlayerInMessage> SeekRequest(int64_t position);
  static std::unique_ptr<MediaPlayerInMessage> SetGainRequest(float gain);

  MediaPlayerInMessageType type_;
  std::unique_ptr<MediaPlayerTimeCheckRequest> time_check_request_;
  std::unique_ptr<MediaPlayerSetHttpSourceRequest> set_http_source_request_;
  // Play has no parameters.
  // Pause has no parameters.
  std::unique_ptr<MediaPlayerSeekRequest> seek_request_;
  std::unique_ptr<MediaPlayerSetGainRequest> set_gain_request_;
};

// Union-like of all possible messages sent by the stub and handled
// by the proxy.
struct MediaPlayerOutMessage {
  static std::unique_ptr<MediaPlayerOutMessage> TimeCheckResponse(
      int64_t requestor_time, int64_t responder_time);
  static std::unique_ptr<MediaPlayerOutMessage> StatusNotification(
      fuchsia::mediaplayer::MediaPlayerStatusPtr status);

  MediaPlayerOutMessageType type_;
  std::unique_ptr<MediaPlayerTimeCheckResponse> time_check_response_;
  std::unique_ptr<MediaPlayerStatusNotification> status_notification_;
};

// Serialization overrides.
Serializer& operator<<(Serializer& serializer, fidl::StringPtr value);
Serializer& operator<<(Serializer& serializer, MediaPlayerInMessageType value);
Serializer& operator<<(Serializer& serializer, MediaPlayerOutMessageType value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckResponse>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerSetHttpSourceRequest>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerSeekRequest>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerSetGainRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerStatusNotification>& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::mediaplayer::MediaPlayerStatusPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::media::TimelineTransformPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::mediaplayer::MetadataPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::mediaplayer::Property& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::mediaplayer::ProblemPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const fuchsia::math::SizePtr& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerInMessage>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerOutMessage>& value);
template <typename T>
Serializer& operator<<(Serializer& serializer,
                       const fidl::VectorPtr<T>& value) {
  FXL_DCHECK(value);
  serializer << value->size();
  for (auto& element : *value) {
    serializer << element;
  }

  return serializer;
}

// Deserialization overrides.
Deserializer& operator>>(Deserializer& deserializer, fidl::StringPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerInMessageType& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerOutMessageType& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckResponse>& value);
Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<MediaPlayerSetHttpSourceRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSeekRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSetGainRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerStatusNotification>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::mediaplayer::MediaPlayerStatusPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::media::TimelineTransformPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::mediaplayer::MetadataPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::mediaplayer::Property& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::mediaplayer::ProblemPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         fuchsia::math::SizePtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerInMessage>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerOutMessage>& value);
template <typename T>
Deserializer& operator>>(Deserializer& deserializer,
                         fidl::VectorPtr<T>& value) {
  size_t size;
  deserializer >> size;
  value.reset();
  value.resize(size);
  for (size_t i = 0; i < size; ++i) {
    deserializer >> (*value)[i];
  }

  return deserializer;
}

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_MEDIA_PLAYER_MESSAGES_H_
