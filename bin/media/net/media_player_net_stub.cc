// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net/media_player_net_stub.h"

#include <vector>

#include <mx/channel.h>

#include "apps/media/lib/timeline/timeline.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/ftl/logging.h"

namespace media {

MediaPlayerNetStub::MediaPlayerNetStub(
    MediaPlayer* player,
    mx::channel channel,
    netconnector::NetStubResponder<MediaPlayer, MediaPlayerNetStub>* responder)
    : player_(player), responder_(responder) {
  FTL_DCHECK(player_);
  FTL_DCHECK(responder_);

  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  message_relay_.SetChannelClosedCallback(
      [this]() { responder_->ReleaseStub(shared_from_this()); });

  message_relay_.SetChannel(std::move(channel));
}

MediaPlayerNetStub::~MediaPlayerNetStub() {}

void MediaPlayerNetStub::HandleReceivedMessage(std::vector<uint8_t> message) {
  if (message.size() == 0) {
    FTL_LOG(ERROR) << "Zero-sized message received";
    message_relay_.CloseChannel();
    return;
  }

  switch (static_cast<MessageType>(message.data()[0])) {
    case MessageType::kTimeCheck: {
      TimeCheckMessage* time_check_message =
          MessageCast<TimeCheckMessage>(&message);
      if (time_check_message != nullptr) {
        time_check_message->receiver_time_ = time_check_message->sender_time_;
        time_check_message->sender_time_ = Timeline::local_now();

        time_check_message->HostToNet();
        message_relay_.SendMessage(message);

        // Do this here so we never send a status message before we respond
        // to the initial time check message.
        HandleStatusUpdates();
      }
    } break;

    case MessageType::kPlay:
      if (MessageCast<PlayMessage>(&message) != nullptr) {
        player_->Play();
      }
      break;

    case MessageType::kPause:
      if (MessageCast<PauseMessage>(&message) != nullptr) {
        player_->Pause();
      }
      break;

    case MessageType::kSeek: {
      SeekMessage* seek_message = MessageCast<SeekMessage>(&message);
      if (seek_message != nullptr) {
        player_->Seek(seek_message->position_);
      }
    } break;

    default:
      FTL_LOG(ERROR) << "Unrecognized packet type " << message.data()[0];
      message_relay_.CloseChannel();
      return;
  }
}

void MediaPlayerNetStub::HandleStatusUpdates(uint64_t version,
                                             MediaPlayerStatusPtr status) {
  if (status) {
    std::vector<uint8_t> message;
    StatusMessage* status_message = NewMessage<StatusMessage>(&message);

    if (!status->timeline_transform) {
      status_message->reference_time_ = kUnspecifiedTime;
      status_message->subject_time_ = kUnspecifiedTime;
      status_message->reference_delta_ = 0;
      status_message->subject_delta_ = 0;
    } else {
      status_message->reference_time_ =
          status->timeline_transform->reference_time;
      status_message->subject_time_ = status->timeline_transform->subject_time;
      status_message->reference_delta_ =
          status->timeline_transform->reference_delta;
      status_message->subject_delta_ =
          status->timeline_transform->subject_delta;
    }

    status_message->end_of_stream_ = status->end_of_stream;

    if (status->metadata) {
      status_message->duration_ = status->metadata->duration;
    } else {
      status_message->duration_ = 0;
    }

    status_message->HostToNet();
    message_relay_.SendMessage(message);
  }

  // Request a status update.
  player_->GetStatus(
      version,
      [weak_this = std::weak_ptr<MediaPlayerNetStub>(shared_from_this())](
          uint64_t version, MediaPlayerStatusPtr status) {
        auto shared_this = weak_this.lock();
        if (shared_this) {
          shared_this->HandleStatusUpdates(version, std::move(status));
        }
      });
}

}  // namespace media
