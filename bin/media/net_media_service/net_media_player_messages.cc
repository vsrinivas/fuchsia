// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net/media_player_messages.h"

#include "lib/ftl/logging.h"

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
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::Play() {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kPlay;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::Pause() {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kPause;
  return message;
}

// static
std::unique_ptr<MediaPlayerInMessage> MediaPlayerInMessage::Seek(
    int64_t position) {
  std::unique_ptr<MediaPlayerInMessage> message =
      std::make_unique<MediaPlayerInMessage>();
  message->type_ = MediaPlayerInMessageType::kSeek;
  message->seek_ = std::make_unique<MediaPlayerSeekRequest>();
  message->seek_->position_ = position;
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
std::unique_ptr<MediaPlayerOutMessage> MediaPlayerOutMessage::Status(
    MediaPlayerStatusPtr status) {
  std::unique_ptr<MediaPlayerOutMessage> message =
      std::make_unique<MediaPlayerOutMessage>();
  message->type_ = MediaPlayerOutMessageType::kStatus;
  message->status_ = std::make_unique<MediaPlayerStatusNotification>();
  message->status_->status_ = std::move(status);
  return message;
}

Serializer& operator<<(Serializer& serializer, const fidl::String& value) {
  serializer << value.size();
  serializer.PutBytes(value.size(), value.data());
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
  FTL_DCHECK(value);
  return serializer << value->requestor_time_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerTimeCheckResponse>& value) {
  FTL_DCHECK(value);
  return serializer << value->requestor_time_ << value->responder_time_;
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerSeekRequest>& value) {
  FTL_DCHECK(value);
  return serializer << value->position_;
}

Serializer& operator<<(
    Serializer& serializer,
    const std::unique_ptr<MediaPlayerStatusNotification>& value) {
  FTL_DCHECK(value);
  return serializer << value->status_;
}

Serializer& operator<<(Serializer& serializer,
                       const MediaPlayerStatusPtr& value) {
  FTL_DCHECK(value);
  return serializer << Optional(value->timeline_transform)
                    << value->end_of_stream << Optional(value->metadata)
                    << Optional(value->problem);
}

Serializer& operator<<(Serializer& serializer,
                       const TimelineTransformPtr& value) {
  FTL_DCHECK(value);
  return serializer << value->reference_time << value->subject_time
                    << value->reference_delta << value->subject_delta;
}

Serializer& operator<<(Serializer& serializer, const MediaMetadataPtr& value) {
  FTL_DCHECK(value);
  return serializer << value->duration << Optional(value->title)
                    << Optional(value->artist) << Optional(value->album)
                    << Optional(value->publisher) << Optional(value->genre)
                    << Optional(value->composer);
}

Serializer& operator<<(Serializer& serializer, const ProblemPtr& value) {
  FTL_DCHECK(value);
  return serializer << value->type << Optional(value->details);
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerInMessage>& value) {
  FTL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case MediaPlayerInMessageType::kTimeCheckRequest:
      serializer << value->time_check_request_;
      break;
    case MediaPlayerInMessageType::kPlay:
    case MediaPlayerInMessageType::kPause:
      // These two have no parameters.
      break;
    case MediaPlayerInMessageType::kSeek:
      serializer << value->seek_;
      break;
  }

  return serializer;
}

Serializer& operator<<(Serializer& serializer,
                       const std::unique_ptr<MediaPlayerOutMessage>& value) {
  FTL_DCHECK(value);
  serializer << value->type_;
  switch (value->type_) {
    case MediaPlayerOutMessageType::kTimeCheckResponse:
      serializer << value->time_check_response_;
      break;
    case MediaPlayerOutMessageType::kStatus:
      serializer << value->status_;
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

Deserializer& operator>>(Deserializer& deserializer,
                         std::unique_ptr<MediaPlayerSeekRequest>& value) {
  value = std::make_unique<MediaPlayerSeekRequest>();
  deserializer >> value->position_;
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
    case MediaPlayerInMessageType::kPlay:
    case MediaPlayerInMessageType::kPause:
      // These two have no parameters.
      break;
    case MediaPlayerInMessageType::kSeek:
      deserializer >> value->seek_;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported media player in-message type "
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
    case MediaPlayerOutMessageType::kStatus:
      deserializer >> value->status_;
      break;
    default:
      FTL_LOG(ERROR) << "Unsupported media player out-message type "
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
