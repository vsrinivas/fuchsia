// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/media/services/net_media_player.fidl.h"
#include "apps/media/src/net_media_service/serialization.h"

namespace media {

// The definitions below are for messages that are serialized and exchanged
// between a net media player and a control point. The proxy resides at the
// control point and the stub is adjacent to the net media player.

// Types of messages sent by the proxy and handled by the stub.
enum class NetMediaPlayerInMessageType : uint8_t {
  kTimeCheckRequest,
  kSetUrlRequest,
  kPlayRequest,
  kPauseRequest,
  kSeekRequest
};

// Types of messages sent by the stub and handled by the proxy.
enum class NetMediaPlayerOutMessageType : uint8_t {
  kTimeCheckResponse,
  kStatusNotification
};

// Sent by the proxy to establish a correlation between system times on
// the two systems.
struct NetMediaPlayerTimeCheckRequest {
  int64_t requestor_time_;  // System time when this message was sent.
};

// Sent by the stub in response to |NetMediaPlayerTimeCheckRequest| to
// establish a correlation between system times on the two systems.
struct NetMediaPlayerTimeCheckResponse {
  int64_t requestor_time_;  // From the request
  int64_t responder_time_;  // System time when this message was sent.
};

// Sent by the proxy to request a url change.
struct NetMediaPlayerSetUrlRequest {
  fidl::String url_;
};

// Play and Pause have no parameters, so there is no NetMediaPlayerPlayRequest
// or NetMediaPlayerPauseRequest.

// Sent by the proxy to request a seek.
struct NetMediaPlayerSeekRequest {
  int64_t position_;
};

// Sent by the stub to notify the proxy of a change in status.
struct NetMediaPlayerStatusNotification {
  MediaPlayerStatusPtr status_;
};

// Union-like of all possible messages sent by the proxy and handled
// by the stub.
struct NetMediaPlayerInMessage {
  static std::unique_ptr<NetMediaPlayerInMessage> TimeCheckRequest(
      int64_t requestor_time);
  static std::unique_ptr<NetMediaPlayerInMessage> SetUrlRequest(
      const fidl::String& url);
  static std::unique_ptr<NetMediaPlayerInMessage> PlayRequest();
  static std::unique_ptr<NetMediaPlayerInMessage> PauseRequest();
  static std::unique_ptr<NetMediaPlayerInMessage> SeekRequest(int64_t position);

  NetMediaPlayerInMessageType type_;
  std::unique_ptr<NetMediaPlayerTimeCheckRequest> time_check_request_;
  std::unique_ptr<NetMediaPlayerSetUrlRequest> set_url_request_;
  // Play has no parameters.
  // Pause has no parameters.
  std::unique_ptr<NetMediaPlayerSeekRequest> seek_request_;
};

// Union-like of all possible messages sent by the stub and handled
// by the proxy.
struct NetMediaPlayerOutMessage {
  static std::unique_ptr<NetMediaPlayerOutMessage> TimeCheckResponse(
      int64_t requestor_time,
      int64_t responder_time);
  static std::unique_ptr<NetMediaPlayerOutMessage> StatusNotification(
      MediaPlayerStatusPtr status);

  NetMediaPlayerOutMessageType type_;
  std::unique_ptr<NetMediaPlayerTimeCheckResponse> time_check_response_;
  std::unique_ptr<NetMediaPlayerStatusNotification> status_notification_;
};

// Serialization overrides.
Serializer& operator<<(Serializer& serializer, const fidl::String& value);
Serializer& operator<<(Serializer& serializer,
                       NetMediaPlayerInMessageType value);
Serializer& operator<<(Serializer& serializer,
                       NetMediaPlayerOutMessageType value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerTimeCheckRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerTimeCheckResponse>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerSetUrlRequest>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<NetMediaPlayerSeekRequest>& value);
Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerStatusNotification>& value);
Serializer& operator<<(Serializer& serializer,
                       const MediaPlayerStatusPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const TimelineTransformPtr& value);
Serializer& operator<<(Serializer& serializer, const MediaMetadataPtr& value);
Serializer& operator<<(Serializer& serializer, const ProblemPtr& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<NetMediaPlayerInMessage>& value);
Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<NetMediaPlayerOutMessage>& value);

// Deserialization overrides.
Deserializer& operator>>(Deserializer& deserializer, fidl::String& value);
Deserializer& operator>>(Deserializer& deserializer,
                         NetMediaPlayerInMessageType& value);
Deserializer& operator>>(Deserializer& deserializer,
                         NetMediaPlayerOutMessageType& value);
Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerTimeCheckRequest>& value);
Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerTimeCheckResponse>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerSetUrlRequest>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerSeekRequest>& value);
Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerStatusNotification>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerStatusPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         TimelineTransformPtr& value);
Deserializer& operator>>(Deserializer& deserializer, MediaMetadataPtr& value);
Deserializer& operator>>(Deserializer& deserializer, ProblemPtr& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerInMessage>& value);
Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerOutMessage>& value);

}  // namespace media
