// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/media/services/media_player.fidl.h"
#include "apps/media/src/net/serialization.h"

namespace media {

// The definitions below are for messages that are serialized and exchanged
// between a media player and a control point. The 'player proxy' resides at
// the control point and the 'player stub' is adjacent to the media player.

// Types of messages sent by the player proxy and handled by the player stub.
enum class MediaPlayerInMessageType : uint8_t {
  kTimeCheckRequest,
  kPlay,
  kPause,
  kSeek
};

// Types of messages sent by the player stub and handled by the player proxy.
enum class MediaPlayerOutMessageType : uint8_t { kTimeCheckResponse, kStatus };

// Sent by the player proxy to establish a correlation between system times on
// the two systems.
struct MediaPlayerTimeCheckRequest {
  int64_t requestor_time_;  // System time when this message was sent.
};

// Sent by the player stub in response to |MediaPlayerTimeCheckRequest| to
// establish a correlation between system times on the two systems.
struct MediaPlayerTimeCheckResponse {
  int64_t requestor_time_;  // From the request
  int64_t responder_time_;  // System time when this message was sent.
};

// Play and Pause have no parameters, so there is no MediaPlayerPlayRequest or
// MediaPlayerPauseRequest.

// Sent by the player proxy to request a seek.
struct MediaPlayerSeekRequest {
  int64_t position_;
};

// Sent by the player stub to notify the proxy of a change in status.
struct MediaPlayerStatusNotification {
  MediaPlayerStatusPtr status_;
};

// Union-like of all possible messages sent by the player proxy and handled
// by the player stub.
struct MediaPlayerInMessage {
  static std::unique_ptr<MediaPlayerInMessage> TimeCheckRequest(
      int64_t requestor_time);
  static std::unique_ptr<MediaPlayerInMessage> Play();
  static std::unique_ptr<MediaPlayerInMessage> Pause();
  static std::unique_ptr<MediaPlayerInMessage> Seek(int64_t position);

  MediaPlayerInMessageType type_;
  std::unique_ptr<MediaPlayerTimeCheckRequest> time_check_request_;
  // Play has no parameters.
  // Pause has no parameters.
  std::unique_ptr<MediaPlayerSeekRequest> seek_;
};

// Union-like of all possible messages sent by the player stub and handled
// by the player proxy.
struct MediaPlayerOutMessage {
  static std::unique_ptr<MediaPlayerOutMessage> TimeCheckResponse(
      int64_t requestor_time,
      int64_t responder_time);
  static std::unique_ptr<MediaPlayerOutMessage> Status(
      MediaPlayerStatusPtr status);

  MediaPlayerOutMessageType type_;
  std::unique_ptr<MediaPlayerTimeCheckResponse> time_check_response_;
  std::unique_ptr<MediaPlayerStatusNotification> status_;
};

// Serialization overrides.
Serializer& operator<<(Serializer& serializer, const fidl::String& value);
Serializer& operator<<(Serializer& serializer, MediaPlayerInMessageType value);
Serializer& operator<<(Serializer& serializer, MediaPlayerOutMessageType value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckResponse>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerSeekRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerStatusNotification>& value);
Serializer& operator<<(Serializer& serializer,
                       const MediaPlayerStatusPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const TimelineTransformPtr& value);
Serializer& operator<<(Serializer& serializer, const MediaMetadataPtr& value);
Serializer& operator<<(Serializer& serializer, const ProblemPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerInMessage>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerOutMessage>& value);

// Deserialization overrides.
Deserializer& operator>>(Deserializer& deserializer, fidl::String& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerInMessageType& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerOutMessageType& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckResponse>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSeekRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerStatusNotification>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerStatusPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         TimelineTransformPtr& value);
Deserializer& operator>>(Deserializer& deserializer, MediaMetadataPtr& value);
Deserializer& operator>>(Deserializer& deserializer, ProblemPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerInMessage>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerOutMessage>& value);

}  // namespace media
