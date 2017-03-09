// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net/media_player_net_stub.h"

#include <vector>

#include <mx/channel.h>

#include "application/lib/app/application_context.h"
#include "apps/media/lib/timeline/timeline.h"
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

void MediaPlayerNetStub::HandleReceivedMessage(
    std::vector<uint8_t> serial_message) {
  std::unique_ptr<MediaPlayerInMessage> message;
  Deserializer deserializer(serial_message);
  deserializer >> message;

  if (!deserializer.complete()) {
    FTL_LOG(ERROR) << "Malformed message received";
    message_relay_.CloseChannel();
    return;
  }

  FTL_DCHECK(message);

  switch (message->type_) {
    case MediaPlayerInMessageType::kTimeCheckRequest:
      FTL_DCHECK(message->time_check_request_);
      message_relay_.SendMessage(
          Serializer::Serialize(MediaPlayerOutMessage::TimeCheckResponse(
              message->time_check_request_->requestor_time_,
              Timeline::local_now())));

      // Do this here so we never send a status message before we respond
      // to the initial time check message.
      HandleStatusUpdates();
      break;

    case MediaPlayerInMessageType::kPlay:
      player_->Play();
      break;

    case MediaPlayerInMessageType::kPause:
      player_->Pause();
      break;

    case MediaPlayerInMessageType::kSeek:
      FTL_DCHECK(message->seek_);
      player_->Seek(message->seek_->position_);
      break;
  }
}

void MediaPlayerNetStub::HandleStatusUpdates(uint64_t version,
                                             MediaPlayerStatusPtr status) {
  if (status) {
    message_relay_.SendMessage(Serializer::Serialize(
        MediaPlayerOutMessage::Status(std::move(status))));
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
