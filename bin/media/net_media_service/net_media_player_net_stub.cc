// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/net_media_player_net_stub.h"

#include <vector>

#include <mx/channel.h>

#include "lib/app/cpp/application_context.h"
#include "lib/media/timeline/timeline.h"
#include "lib/ftl/logging.h"

namespace media {

NetMediaPlayerNetStub::NetMediaPlayerNetStub(
    NetMediaPlayer* player,
    mx::channel channel,
    netconnector::NetStubResponder<NetMediaPlayer, NetMediaPlayerNetStub>*
        responder)
    : player_(player), responder_(responder) {
  FTL_DCHECK(player_);
  FTL_DCHECK(responder_);

  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  message_relay_.SetChannelClosedCallback(
      [this]() { responder_->ReleaseStub(shared_from_this()); });

  message_relay_.SetChannel(std::move(channel));
}

NetMediaPlayerNetStub::~NetMediaPlayerNetStub() {}

void NetMediaPlayerNetStub::HandleReceivedMessage(
    std::vector<uint8_t> serial_message) {
  std::unique_ptr<NetMediaPlayerInMessage> message;
  Deserializer deserializer(serial_message);
  deserializer >> message;

  if (!deserializer.complete()) {
    FTL_LOG(ERROR) << "Malformed message received";
    message_relay_.CloseChannel();
    return;
  }

  FTL_DCHECK(message);

  switch (message->type_) {
    case NetMediaPlayerInMessageType::kTimeCheckRequest:
      FTL_DCHECK(message->time_check_request_);
      message_relay_.SendMessage(
          Serializer::Serialize(NetMediaPlayerOutMessage::TimeCheckResponse(
              message->time_check_request_->requestor_time_,
              Timeline::local_now())));

      // Do this here so we never send a status message before we respond
      // to the initial time check message.
      HandleStatusUpdates();
      break;

    case NetMediaPlayerInMessageType::kSetUrlRequest:
      FTL_DCHECK(message->set_url_request_);
      player_->SetUrl(message->set_url_request_->url_);
      break;

    case NetMediaPlayerInMessageType::kPlayRequest:
      player_->Play();
      break;

    case NetMediaPlayerInMessageType::kPauseRequest:
      player_->Pause();
      break;

    case NetMediaPlayerInMessageType::kSeekRequest:
      FTL_DCHECK(message->seek_request_);
      player_->Seek(message->seek_request_->position_);
      break;
  }
}

void NetMediaPlayerNetStub::HandleStatusUpdates(uint64_t version,
                                             MediaPlayerStatusPtr status) {
  if (status) {
    message_relay_.SendMessage(Serializer::Serialize(
        NetMediaPlayerOutMessage::StatusNotification(std::move(status))));
  }

  // Request a status update.
  player_->GetStatus(
      version,
      [weak_this = std::weak_ptr<NetMediaPlayerNetStub>(shared_from_this())](
          uint64_t version, MediaPlayerStatusPtr status) {
        auto shared_this = weak_this.lock();
        if (shared_this) {
          shared_this->HandleStatusUpdates(version, std::move(status));
        }
      });
}

}  // namespace media
