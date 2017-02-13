// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_player_net_proxy.h"

#include <vector>

#include <mx/channel.h>

#include "apps/media/lib/timeline/timeline.h"
#include "apps/netconnector/services/netconnector.fidl.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<MediaPlayerNetProxy> MediaPlayerNetProxy::Create(
    std::string device_name,
    std::string service_name,
    fidl::InterfaceRequest<MediaPlayer> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaPlayerNetProxy>(new MediaPlayerNetProxy(
      device_name, service_name, std::move(request), owner));
}

MediaPlayerNetProxy::MediaPlayerNetProxy(
    std::string device_name,
    std::string service_name,
    fidl::InterfaceRequest<MediaPlayer> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaPlayer>(this, std::move(request), owner) {
  FTL_DCHECK(owner);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        callback(version, status_.Clone());
      });

  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  message_relay_.SetChannelClosedCallback(
      [this]() { UnbindAndReleaseFromOwner(); });

  netconnector::NetConnectorPtr connector =
      owner->ConnectToEnvironmentService<netconnector::NetConnector>();

  // Create a pair of channels.
  mx::channel local;
  mx::channel remote;
  mx_status_t status = mx::channel::create(0u, &local, &remote);

  FTL_CHECK(status == NO_ERROR) << "mx::channel::create failed, status "
                                << status;

  // Give the local end of the channel to the relay.
  message_relay_.SetChannel(std::move(local));

  // Pass the remote end to NetConnector.
  app::ServiceProviderPtr device_service_provider;
  connector->GetDeviceServiceProvider(device_name,
                                      device_service_provider.NewRequest());

  device_service_provider->ConnectToService(service_name, std::move(remote));

  SendTimeCheckMessage();
}

MediaPlayerNetProxy::~MediaPlayerNetProxy() {}

void MediaPlayerNetProxy::Play() {
  std::vector<uint8_t> message;
  NewMessage<PlayMessage>(&message);
  message_relay_.SendMessage(message);
}

void MediaPlayerNetProxy::Pause() {
  std::vector<uint8_t> message;
  NewMessage<PauseMessage>(&message);
  message_relay_.SendMessage(message);
}

void MediaPlayerNetProxy::Seek(int64_t position) {
  std::vector<uint8_t> message;
  SeekMessage* seek_message = NewMessage<SeekMessage>(&message);

  seek_message->position_ = position;

  seek_message->HostToNet();
  message_relay_.SendMessage(message);
}

void MediaPlayerNetProxy::GetStatus(uint64_t version_last_seen,
                                    const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaPlayerNetProxy::SendTimeCheckMessage() {
  std::vector<uint8_t> message;
  TimeCheckMessage* time_check_message = NewMessage<TimeCheckMessage>(&message);

  time_check_message->sender_time_ = Timeline::local_now();
  time_check_message->receiver_time_ = 0;

  time_check_message->HostToNet();
  message_relay_.SendMessage(message);
}

void MediaPlayerNetProxy::HandleReceivedMessage(std::vector<uint8_t> message) {
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
        // Assume the sender's time was sampled halfway between the time we
        // sent the original TimeCheckMessage and the time this response
        // arrived (that is, that the transit times there and back are equal).
        // Also endeavor not to overflow.
        int64_t local_then =
            time_check_message->receiver_time_ +
            (Timeline::local_now() - time_check_message->receiver_time_) / 2;

        // Assume our clocks run at the same rate.
        remote_to_local_ = TimelineFunction(time_check_message->sender_time_,
                                            local_then, 1, 1);
      }
    } break;

    case MessageType::kStatus: {
      StatusMessage* status_message = MessageCast<StatusMessage>(&message);
      if (status_message != nullptr) {
        if (remote_to_local_.subject_delta() == 0) {
          FTL_LOG(ERROR) << "Received status message before time check message";
          message_relay_.CloseChannel();
          return;
        }

        if (status_message->reference_time_ == kUnspecifiedTime) {
          // No transform was supplied.
          status_.timeline_transform.reset();
        } else {
          // Transform was supplied.
          if (!status_.timeline_transform) {
            status_.timeline_transform = TimelineTransform::New();
          }

          status_.timeline_transform->reference_time =
              remote_to_local_(status_message->reference_time_);
          status_.timeline_transform->subject_time =
              status_message->subject_time_;
          status_.timeline_transform->reference_delta =
              status_message->reference_delta_;
          status_.timeline_transform->subject_delta =
              status_message->subject_delta_;
        }

        status_.end_of_stream = status_message->end_of_stream_;

        if (status_message->duration_ == 0) {
          status_.metadata.reset();
        } else {
          if (!status_.metadata) {
            status_.metadata = MediaMetadata::New();
          }

          status_.metadata->duration = status_message->duration_;
        }

        status_publisher_.SendUpdates();
      }
    } break;

    default:
      FTL_LOG(ERROR) << "Unrecognized packet type " << message.data()[0];
      message_relay_.CloseChannel();
      return;
  }
}

}  // namespace media
