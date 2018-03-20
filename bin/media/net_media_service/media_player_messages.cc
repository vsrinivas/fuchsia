// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/media_player_messages.h"

#include "lib/fxl/logging.h"

namespace media {

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::TimeCheckRequest(
    int64_t requestor_time) {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kTimeCheckRequest;
  message->time_check_request_ =
      std::make_unique<MediaPlayerTimeCheckRequest>();
  message->time_check_request_->requestor_time_ = requestor_time;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage>
MediaPlayerInMessage::SetHttpSourceRequest(const f1dl::StringPtr& url) {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kSetHttpSourceRequest;
  message->set_http_source_request_ =
      std::make_unique<MediaPlayerSetHttpSourceRequest>();
  message->set_http_source_request_->url_ = url;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::PlayRequest() {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kPlayRequest;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::PauseRequest() {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kPauseRequest;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::SeekRequest(
    int64_t position) {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kSeekRequest;
  message->seek_request_ = std::make_unique<MediaPlayerSeekRequest>();
  message->seek_request_->position_ = position;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::SetGainRequest(
    float gain) {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kSetGainRequest;
  message->set_gain_request_ = std::make_unique<MediaPlayerSetGainRequest>();
  message->set_gain_request_->gain_ = gain;
  return message;
}

// static
std::unique_ptr<MediaPlayerOutMessage> MediaPlayerOutMessage::TimeCheckResponse(
    int64_t requestor_time,
    int64_t responder_time) {
  std::unique_ptr<MediaPlayerOutMessage> message =
      std::make_unique<MediaPlayerOutMessage>();
  message->type_ = MediaPlayerOutMessageType::kTimeCheckResponse;
  message->time_check_response_ =
      std::make_unique<MediaPlayerTimeCheckResponse>();
  message->time_check_response_->requestor_time_ = requestor_time;
  message->time_check_response_->responder_time_ = responder_time;
  return message;
}

// static
std::unique_ptr<MediaPlayerOutMessage>
MediaPlayerOutMessage::StatusNotification(MediaPlayerStatusPtr status) {
  std::unique_ptr<MediaPlayerOutMessage> message =
      std::make_unique<MediaPlayerOutMessage>();
  message->type_ = MediaPlayerOutMessageType::kStatusNotification;
  message->status_notification_ =
      std::make_unique<MediaPlayerStatusNotification>();
  message->status_notification_->status_ = std::move(status);
  return message;
}

Serializer& operator<<(Serializer& serializer, const f1dl::StringPtr& value) {
  serializer << value->size();
  serializer.PutBytes(value->size(), value->data());
  return serializer;
}

Serializer& operator<<(Serializer& serializer, MediaPlayerInMessageType value) {
  return serializer << static_cast<uint8_t>(value);
}

Serializer& operator<<(Serializer& serializer,
                       MediaPlayerOutMessageType value) {
  return serializer << static_cast<uint8_t>(value);
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->requestor_time_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckResponse>& value) {
  FXL_DCHECK(value);
  return serializer << value->requestor_time_ << value->responder_time_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerSetHttpSourceRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->url_;
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerSeekRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->position_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerSetGainRequest>& value) {
  FXL_DCHECK(value);
  return serializer << value->gain_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerStatusNotification>& value) {
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
                       const std::unique_ptr<MediaPlayerInMessage>& value) {
  FXL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case MediaPlayerInMessageType::kTimeCheckRequest:
      serializer << value->time_check_request_;
      break;
    case MediaPlayerInMessageType::kSetHttpSourceRequest:
      serializer << value->set_http_source_request_;
      break;
    case MediaPlayerInMessageType::kPlayRequest:
    case MediaPlayerInMessageType::kPauseRequest:
      // These two have no parameters.
      break;
    case MediaPlayerInMessageType::kSeekRequest:
      serializer << value->seek_request_;
      break;
    case MediaPlayerInMessageType::kSetGainRequest:
      serializer << value->set_gain_request_;
      break;
  }

  return serializer;
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerOutMessage>& value) {
  FXL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case MediaPlayerOutMessageType::kTimeCheckResponse:
      serializer << value->time_check_response_;
      break;
    case MediaPlayerOutMessageType::kStatusNotification:
      serializer << value->status_notification_;
      break;
  }

  return serializer;
}

Deserializer& operator>>(Deserializer& deserializer, f1dl::StringPtr& value) {
  size_t size;
  deserializer >> size;
  const char* bytes = reinterpret_cast<const char*>(deserializer.Bytes(size));
  if (bytes == nullptr) {
    value = f1dl::StringPtr();
  } else {
    value = f1dl::StringPtr(bytes, size);
  }

  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerInMessageType& value) {
  deserializer.GetBytes(sizeof(value), &value);
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         MediaPlayerOutMessageType& value) {
  deserializer.GetBytes(sizeof(value), &value);
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckRequest>& value) {
  value = std::make_unique<MediaPlayerTimeCheckRequest>();
  deserializer >> value->requestor_time_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerTimeCheckResponse>& value) {
  value = std::make_unique<MediaPlayerTimeCheckResponse>();
  deserializer >> value->requestor_time_ >> value->responder_time_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<MediaPlayerSetHttpSourceRequest>& value) {
  value = std::make_unique<MediaPlayerSetHttpSourceRequest>();
  deserializer >> value->url_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSeekRequest>& value) {
  value = std::make_unique<MediaPlayerSeekRequest>();
  deserializer >> value->position_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSetGainRequest>& value) {
  value = std::make_unique<MediaPlayerSetGainRequest>();
  deserializer >> value->gain_;
  if (!deserializer.healthy()) {
    value.reset();
  }
  return deserializer;
}

Deserializer& operator>>(
    Deserializer& deserializer,
    std::unique_ptr<MediaPlayerStatusNotification>& value) {
  value = std::make_unique<MediaPlayerStatusNotification>();
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
                         std::unique_ptr<MediaPlayerInMessage>& value) {
  value = std::make_unique<MediaPlayerInMessage>();
  deserializer >> value->type_;

  switch (value->type_) {
    case MediaPlayerInMessageType::kTimeCheckRequest:
      deserializer >> value->time_check_request_;
      break;
    case MediaPlayerInMessageType::kSetHttpSourceRequest:
      deserializer >> value->set_http_source_request_;
      break;
    case MediaPlayerInMessageType::kPlayRequest:
    case MediaPlayerInMessageType::kPauseRequest:
      // These two have no parameters.
      break;
    case MediaPlayerInMessageType::kSeekRequest:
      deserializer >> value->seek_request_;
      break;
    case MediaPlayerInMessageType::kSetGainRequest:
      deserializer >> value->set_gain_request_;
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
                         std::unique_ptr<MediaPlayerOutMessage>& value) {
  value = std::make_unique<MediaPlayerOutMessage>();
  deserializer >> value->type_;

  switch (value->type_) {
    case MediaPlayerOutMessageType::kTimeCheckResponse:
      deserializer >> value->time_check_response_;
      break;
    case MediaPlayerOutMessageType::kStatusNotification:
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
