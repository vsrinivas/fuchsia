// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/media_player_net_stub.h"

#include <vector>

#include <lib/zx/channel.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

MediaPlayerNetStub::MediaPlayerNetStub(
    MediaPlayer* player,
    zx::channel channel,
    netconnector::NetStubResponder<MediaPlayer, MediaPlayerNetStub>* responder)
    : player_(player), responder_(responder) {
  FXL_DCHECK(player_);
  FXL_DCHECK(responder_);

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
    FXL_LOG(ERROR) << "Malformed message received";
    message_relay_.CloseChannel();
    return;
  }

  FXL_DCHECK(message);

  switch (message->type_) {
    case MediaPlayerInMessageType::kTimeCheckRequest:
      FXL_DCHECK(message->time_check_request_);
      message_relay_.SendMessage(
          Serializer::Serialize(MediaPlayerOutMessage::TimeCheckResponse(
              message->time_check_request_->requestor_time_,
              media::Timeline::local_now())));

      // Do this here so we never send a status message before we respond
      // to the initial time check message.
      HandleStatusUpdates();
      break;

    case MediaPlayerInMessageType::kSetHttpSourceRequest:
      FXL_DCHECK(message->set_http_source_request_);
      player_->SetHttpSource(message->set_http_source_request_->url_);
      break;

    case MediaPlayerInMessageType::kPlayRequest:
      player_->Play();
      break;

    case MediaPlayerInMessageType::kPauseRequest:
      player_->Pause();
      break;

    case MediaPlayerInMessageType::kSeekRequest:
      FXL_DCHECK(message->seek_request_);
      player_->Seek(message->seek_request_->position_);
      break;

    case MediaPlayerInMessageType::kSetGainRequest:
      FXL_DCHECK(message->set_gain_request_);
      player_->SetGain(message->set_gain_request_->gain_);
      break;
  }
}

void MediaPlayerNetStub::HandleStatusUpdates(uint64_t version,
                                             MediaPlayerStatusPtr status) {
  if (status) {
    message_relay_.SendMessage(Serializer::Serialize(
        MediaPlayerOutMessage::StatusNotification(std::move(status))));
  }

  // Request a status update.
  player_->GetStatus(
      version,
      [weak_this = std::weak_ptr<MediaPlayerNetStub>(shared_from_this())](
          uint64_t version, MediaPlayerStatus status) {
        auto shared_this = weak_this.lock();
        if (shared_this) {
          shared_this->HandleStatusUpdates(
              version, fidl::MakeOptional(std::move(status)));
        }
      });
}

}  // namespace media_player
