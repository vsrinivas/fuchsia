// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net_media_service/net_media_player_net_proxy.h"

#include <vector>

#include <mx/channel.h>

#include "apps/media/lib/timeline/timeline.h"
#include "lib/ftl/logging.h"
#include "lib/netconnector/fidl/netconnector.fidl.h"

namespace media {

// static
std::shared_ptr<NetMediaPlayerNetProxy> NetMediaPlayerNetProxy::Create(
    const fidl::String& device_name,
    const fidl::String& service_name,
    fidl::InterfaceRequest<NetMediaPlayer> request,
    NetMediaServiceImpl* owner) {
  return std::shared_ptr<NetMediaPlayerNetProxy>(new NetMediaPlayerNetProxy(
      device_name, service_name, std::move(request), owner));
}

NetMediaPlayerNetProxy::NetMediaPlayerNetProxy(
    const fidl::String& device_name,
    const fidl::String& service_name,
    fidl::InterfaceRequest<NetMediaPlayer> request,
    NetMediaServiceImpl* owner)
    : NetMediaServiceImpl::Product<NetMediaPlayer>(this,
                                                   std::move(request),
                                                   owner),
      status_(MediaPlayerStatus::New()) {
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

  FTL_CHECK(status == MX_OK) << "mx::channel::create failed, status "
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

NetMediaPlayerNetProxy::~NetMediaPlayerNetProxy() {}

void NetMediaPlayerNetProxy::SetUrl(const fidl::String& url) {
  message_relay_.SendMessage(
      Serializer::Serialize(NetMediaPlayerInMessage::SetUrlRequest(url)));
}

void NetMediaPlayerNetProxy::Play() {
  message_relay_.SendMessage(
      Serializer::Serialize(NetMediaPlayerInMessage::PlayRequest()));
}

void NetMediaPlayerNetProxy::Pause() {
  message_relay_.SendMessage(
      Serializer::Serialize(NetMediaPlayerInMessage::PauseRequest()));
}

void NetMediaPlayerNetProxy::Seek(int64_t position) {
  message_relay_.SendMessage(
      Serializer::Serialize(NetMediaPlayerInMessage::SeekRequest(position)));
}

void NetMediaPlayerNetProxy::GetStatus(uint64_t version_last_seen,
                                       const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void NetMediaPlayerNetProxy::SendTimeCheckMessage() {
  message_relay_.SendMessage(Serializer::Serialize(
      NetMediaPlayerInMessage::TimeCheckRequest(Timeline::local_now())));
}

void NetMediaPlayerNetProxy::HandleReceivedMessage(
    std::vector<uint8_t> serial_message) {
  std::unique_ptr<NetMediaPlayerOutMessage> message;
  Deserializer deserializer(serial_message);
  deserializer >> message;

  if (!deserializer.complete()) {
    FTL_LOG(ERROR) << "Malformed message received";
    message_relay_.CloseChannel();
    return;
  }

  FTL_DCHECK(message);

  switch (message->type_) {
    case NetMediaPlayerOutMessageType::kTimeCheckResponse: {
      FTL_DCHECK(message->time_check_response_);
      // Estimate the local system system time when the responder's clock was
      // samples on the remote machine. Assume the clock was sampled halfway
      // between the time we sent the original TimeCheckRequestMessage and the
      // time this TimeCheckResponseMessage arrived. In other words, assume that
      // the transit times there and back are equal. Normally, one would
      // calculate the average of two values with (a + b) / 2. We use
      // a + (b - a) / 2, because it's less likely to overflow.
      int64_t local_then = message->time_check_response_->requestor_time_ +
                           (Timeline::local_now() -
                            message->time_check_response_->requestor_time_) /
                               2;

      // Create a function that translates remote system time to local system
      // time. We assume that both clocks run at the same rate (hence 1, 1).
      remote_to_local_ = TimelineFunction(
          message->time_check_response_->responder_time_, local_then, 1, 1);
    } break;

    case NetMediaPlayerOutMessageType::kStatusNotification:
      FTL_DCHECK(message->status_notification_);
      status_ = std::move(message->status_notification_->status_);
      if (status_->timeline_transform) {
        // Use the remote-to-local conversion established after the time check
        // transaction to translate reference time into local system time.
        status_->timeline_transform->reference_time =
            remote_to_local_(status_->timeline_transform->reference_time);
      }
      status_publisher_.SendUpdates();
      break;
  }
}

}  // namespace media
