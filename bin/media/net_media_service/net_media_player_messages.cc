// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/net_media_player_messages.h"

#include "lib/fxl/logging.h"

namespace media {

// static
std::unique_ptr<NetMediaPlayerInMessage>
NetMediaPlayerInMessage::TimeCheckRequest(int64_t requestor_time) {
  std::unique_ptr<NetMediaPlayerInMessage> message =
      std::make_unique<NetMediaPlayerInMessage>();
  message->type_ = NetMediaPlayerInMessageType::kTimeCheckRequest;
  message->time_check_request_ =
      std::make_unique<NetMediaPlayerTimeCheckRequest>();
  message->time_check_request_->requestor_time_ = requestor_time;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerInMessage> NetMediaPlayerInMessage::SetUrlRequest(
    const fidl::String& url) {
  std::unique_ptr<NetMediaPlayerInMessage> message =
      std::make_unique<NetMediaPlayerInMessage>();
  message->type_ = NetMediaPlayerInMessageType::kSetUrlRequest;
  message->set_url_request_ = std::make_unique<NetMediaPlayerSetUrlRequest>();
  message->set_url_request_->url_ = url;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerInMessage>
NetMediaPlayerInMessage::PlayRequest() {
  std::unique_ptr<NetMediaPlayerInMessage> message =
      std::make_unique<NetMediaPlayerInMessage>();
  message->type_ = NetMediaPlayerInMessageType::kPlayRequest;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerInMessage>
NetMediaPlayerInMessage::PauseRequest() {
  std::unique_ptr<NetMediaPlayerInMessage> message =
      std::make_unique<NetMediaPlayerInMessage>();
  message->type_ = NetMediaPlayerInMessageType::kPauseRequest;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerInMessage> NetMediaPlayerInMessage::SeekRequest(
    int64_t position) {
  std::unique_ptr<NetMediaPlayerInMessage> message =
      std::make_unique<NetMediaPlayerInMessage>();
  message->type_ = NetMediaPlayerInMessageType::kSeekRequest;
  message->seek_request_ = std::make_unique<NetMediaPlayerSeekRequest>();
  message->seek_request_->position_ = position;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerOutMessage>
NetMediaPlayerOutMessage::TimeCheckResponse(int64_t requestor_time,
                                            int64_t responder_time) {
  std::unique_ptr<NetMediaPlayerOutMessage> message =
      std::make_unique<NetMediaPlayerOutMessage>();
  message->type_ = NetMediaPlayerOutMessageType::kTimeCheckResponse;
  message->time_check_response_ =
      std::make_unique<NetMediaPlayerTimeCheckResponse>();
  message->time_check_response_->requestor_time_ = requestor_time;
  message->time_check_response_->responder_time_ = responder_time;
  return message;
}

// static
std::unique_ptr<NetMediaPlayerOutMessage>
NetMediaPlayerOutMessage::StatusNotification(MediaPlayerStatusPtr status) {
  std::unique_ptr<NetMediaPlayerOutMessage> message =
      std::make_unique<NetMediaPlayerOutMessage>();
  message->type_ = NetMediaPlayerOutMessageType::kStatusNotification;
  message->status_notification_ =
      std::make_unique<NetMediaPlayerStatusNotification>();
  message->status_notification_->status_ = std::move(status);
  return message;
}

Serializer& operator<<(Serializer& serializer, const fidl::String& value) {
  serializer << value.size();
  serializer.PutBytes(value.size(), value.data());
  return serializer;
}

Serializer& operator<<(Serializer& serializer,
                       NetMediaPlayerInMessageType value) {
  return serializer << static_cast<uint8_t>(value);
}

Serializer& operator<<(Serializer& serializer,
                       NetMediaPlayerOutMessageType value) {
  return serializer << static_cast<uint8_t>(value);
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerTimeCheckRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->requestor_time_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerTimeCheckResponse>& value) {
  FXL_DCHECK(value);
  return serializer << value->requestor_time_ << value->responder_time_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerSetUrlRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->url_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerSeekRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->position_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<NetMediaPlayerStatusNotification>& value) {
  FXL_DCHECK(value);
  return serializer << value->status_;
}

Serializer& operator<<(Serializer& serializer,
                       const MediaPlayerStatusPtr& value) {
  FXL_DCHECK(value);
  return serializer << Optional(value->timeline_transform)
                    << value->end_of_stream << value->content_has_audio
                    << value->content_has_video << value->audio_connected
                    << value->video_connected << Optional(value->metadata)
                    << Optional(value->problem);
}

Serializer& operator<<(Serializer& serializer,
                       const TimelineTransformPtr& value) {
  FXL_DCHECK(value);
  return serializer << value->reference_time << value->subject_time
                    << value->reference_delta << value->subject_delta;
}

Serializer& operator<<(Serializer& serializer, const MediaMetadataPtr& value) {
  FXL_DCHECK(value);
  return serializer << value->duration << Optional(value->title)
                    << Optional(value->artist) << Optional(value->album)
                    << Optional(value->publisher) << Optional(value->genre)
                    << Optional(value->composer);
}

Serializer& operator<<(Serializer& serializer, const ProblemPtr& value) {
  FXL_DCHECK(value);
  return serializer << value->type << Optional(value->details);
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<NetMediaPlayerInMessage>& value) {
  FXL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case NetMediaPlayerInMessageType::kTimeCheckRequest:
      serializer << value->time_check_request_;
      break;
    case NetMediaPlayerInMessageType::kSetUrlRequest:
      serializer << value->set_url_request_;
      break;
    case NetMediaPlayerInMessageType::kPlayRequest:
    case NetMediaPlayerInMessageType::kPauseRequest:
      // These two have no parameters.
      break;
    case NetMediaPlayerInMessageType::kSeekRequest:
      serializer << value->seek_request_;
      break;
  }

  return serializer;
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<NetMediaPlayerOutMessage>& value) {
  FXL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case NetMediaPlayerOutMessageType::kTimeCheckResponse:
      serializer << value->time_check_response_;
      break;
    case NetMediaPlayerOutMessageType::kStatusNotification:
      serializer << value->status_notification_;
      break;
  }

  return serializer;
}

Deserializer& operator>>(Deserializer& deserializer, fidl::String& value) {
  size_t size;
  deserializer >> size;
  const char* bytes = reinterpret_cast<const char*>(deserializer.Bytes(size));
  if (bytes == nullptr) {
    value = fidl::String();
  } else {
    value = fidl::String(bytes, size);
  }

  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         NetMediaPlayerInMessageType& value) {
  deserializer.GetBytes(sizeof(value), &value);
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         NetMediaPlayerOutMessageType& value) {
  deserializer.GetBytes(sizeof(value), &value);
  return deserializer;
}

Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerTimeCheckRequest>& value) {
  value = std::make_unique<NetMediaPlayerTimeCheckRequest>();
  deserializer >> value->requestor_time_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerTimeCheckResponse>& value) {
  value = std::make_unique<NetMediaPlayerTimeCheckResponse>();
  deserializer >> value->requestor_time_ >> value->responder_time_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerSetUrlRequest>& value) {
  value = std::make_unique<NetMediaPlayerSetUrlRequest>();
  deserializer >> value->url_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerSeekRequest>& value) {
  value = std::make_unique<NetMediaPlayerSeekRequest>();
  deserializer >> value->position_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<NetMediaPlayerStatusNotification>& value) {
  value = std::make_unique<NetMediaPlayerStatusNotification>();
  deserializer >> value->status_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerStatusPtr& value) {
  value = MediaPlayerStatus::New();
  deserializer >> Optional(value->timeline_transform) >> value->end_of_stream >>
      value->content_has_audio >> value->content_has_video >>
      value->audio_connected >> value->video_connected >>
      Optional(value->metadata) >> Optional(value->problem);
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         TimelineTransformPtr& value) {
  value = TimelineTransform::New();
  deserializer >> value->reference_time >> value->subject_time >>
      value->reference_delta >> value->subject_delta;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer, MediaMetadataPtr& value) {
  value = MediaMetadata::New();
  deserializer >> value->duration >> Optional(value->title) >>
      Optional(value->artist) >> Optional(value->album) >>
      Optional(value->publisher) >> Optional(value->genre) >>
      Optional(value->composer);
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer, ProblemPtr& value) {
  value = Problem::New();
  deserializer >> value->type >> Optional(value->details);
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerInMessage>& value) {
  value = std::make_unique<NetMediaPlayerInMessage>();
  deserializer >> value->type_;

  switch (value->type_) {
    case NetMediaPlayerInMessageType::kTimeCheckRequest:
      deserializer >> value->time_check_request_;
      break;
    case NetMediaPlayerInMessageType::kSetUrlRequest:
      deserializer >> value->set_url_request_;
      break;
    case NetMediaPlayerInMessageType::kPlayRequest:
    case NetMediaPlayerInMessageType::kPauseRequest:
      // These two have no parameters.
      break;
    case NetMediaPlayerInMessageType::kSeekRequest:
      deserializer >> value->seek_request_;
      break;
    default:
      FXL_LOG(ERROR) << "Unsupported media player in-message type "
                     << static_cast<uint8_t>(value->type_);
      deserializer.MarkUnhealthy();
      break;
  }

  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<NetMediaPlayerOutMessage>& value) {
  value = std::make_unique<NetMediaPlayerOutMessage>();
  deserializer >> value->type_;

  switch (value->type_) {
    case NetMediaPlayerOutMessageType::kTimeCheckResponse:
      deserializer >> value->time_check_response_;
      break;
    case NetMediaPlayerOutMessageType::kStatusNotification:
      deserializer >> value->status_notification_;
      break;
    default:
      FXL_LOG(ERROR) << "Unsupported media player out-message type "
                     << static_cast<uint8_t>(value->type_);
      deserializer.MarkUnhealthy();
      break;
  }

  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

}  // namespace media
