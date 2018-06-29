// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/net_media_service/media_player_net_proxy.h"

#include <vector>

#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/zx/channel.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

// static
std::shared_ptr<MediaPlayerNetProxy> MediaPlayerNetProxy::Create(
    fidl::StringPtr device_name, fidl::StringPtr service_name,
    fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer> request,
    NetMediaServiceImpl* owner) {
  return std::shared_ptr<MediaPlayerNetProxy>(new MediaPlayerNetProxy(
      device_name, service_name, std::move(request), owner));
}

MediaPlayerNetProxy::MediaPlayerNetProxy(
    fidl::StringPtr device_name, fidl::StringPtr service_name,
    fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer> request,
    NetMediaServiceImpl* owner)
    : NetMediaServiceImpl::MultiClientProduct<
          fuchsia::mediaplayer::MediaPlayer>(this, std::move(request), owner),
      status_(fuchsia::mediaplayer::MediaPlayerStatus::New()) {
  FXL_DCHECK(owner);

  // Fire |StatusChanged| event for the new client.
  SendStatusUpdates();

  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  message_relay_.SetChannelClosedCallback(
      [this]() { UnbindAndReleaseFromOwner(); });

  fuchsia::netconnector::NetConnectorPtr connector =
      owner->ConnectToEnvironmentService<fuchsia::netconnector::NetConnector>();

  // Create a pair of channels.
  zx::channel local;
  zx::channel remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);

  FXL_CHECK(status == ZX_OK) << "zx::channel::create failed, status " << status;

  // Give the local end of the channel to the relay.
  message_relay_.SetChannel(std::move(local));

  // Pass the remote end to NetConnector.
  fuchsia::sys::ServiceProviderPtr device_service_provider;
  connector->GetDeviceServiceProvider(device_name,
                                      device_service_provider.NewRequest());

  device_service_provider->ConnectToService(service_name, std::move(remote));

  SendTimeCheckMessage();
}

MediaPlayerNetProxy::~MediaPlayerNetProxy() {}

void MediaPlayerNetProxy::SetHttpSource(fidl::StringPtr url) {
  message_relay_.SendMessage(
      Serializer::Serialize(MediaPlayerInMessage::SetHttpSourceRequest(url)));
}

void MediaPlayerNetProxy::SetFileSource(zx::channel file_channel) {
  FXL_LOG(ERROR)
      << "SetFileSource called on MediaPlayer proxy - not supported.";
  UnbindAndReleaseFromOwner();
}

void MediaPlayerNetProxy::SetReaderSource(
    fidl::InterfaceHandle<fuchsia::mediaplayer::SeekingReader> reader_handle) {
  FXL_LOG(ERROR)
      << "SetReaderSource called on MediaPlayer proxy - not supported.";
  UnbindAndReleaseFromOwner();
}

void MediaPlayerNetProxy::Play() {
  message_relay_.SendMessage(
      Serializer::Serialize(MediaPlayerInMessage::PlayRequest()));
}

void MediaPlayerNetProxy::Pause() {
  message_relay_.SendMessage(
      Serializer::Serialize(MediaPlayerInMessage::PauseRequest()));
}

void MediaPlayerNetProxy::Seek(int64_t position) {
  message_relay_.SendMessage(
      Serializer::Serialize(MediaPlayerInMessage::SeekRequest(position)));
}

void MediaPlayerNetProxy::SetGain(float gain) {
  message_relay_.SendMessage(
      Serializer::Serialize(MediaPlayerInMessage::SetGainRequest(gain)));
}

void MediaPlayerNetProxy::CreateView(
    fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewManager> view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
        view_owner_request) {
  FXL_LOG(ERROR) << "CreateView called on MediaPlayer proxy - not supported.";
  UnbindAndReleaseFromOwner();
}

void MediaPlayerNetProxy::SetAudioRenderer(
    fidl::InterfaceHandle<fuchsia::media::AudioRenderer2> audio_renderer) {
  FXL_LOG(ERROR)
      << "SetAudioRenderer called on MediaPlayer proxy - not supported.";
  UnbindAndReleaseFromOwner();
}

void MediaPlayerNetProxy::AddBinding(
    fidl::InterfaceRequest<fuchsia::mediaplayer::MediaPlayer> request) {
  MultiClientProduct::AddBinding(std::move(request));

  // Fire |StatusChanged| event for the new client.
  bindings().bindings().back()->events().StatusChanged(fidl::Clone(*status_));
}

void MediaPlayerNetProxy::SendTimeCheckMessage() {
  message_relay_.SendMessage(Serializer::Serialize(
      MediaPlayerInMessage::TimeCheckRequest(media::Timeline::local_now())));
}

void MediaPlayerNetProxy::HandleReceivedMessage(
    std::vector<uint8_t> serial_message) {
  std::unique_ptr<MediaPlayerOutMessage> message;
  Deserializer deserializer(serial_message);
  deserializer >> message;

  if (!deserializer.complete()) {
    FXL_LOG(ERROR) << "Malformed message received";
    message_relay_.CloseChannel();
    return;
  }

  FXL_DCHECK(message);

  switch (message->type_) {
    case MediaPlayerOutMessageType::kTimeCheckResponse: {
      FXL_DCHECK(message->time_check_response_);
      // Estimate the local system system time when the responder's clock was
      // samples on the remote machine. Assume the clock was sampled halfway
      // between the time we sent the original TimeCheckRequestMessage and the
      // time this TimeCheckResponseMessage arrived. In other words, assume that
      // the transit times there and back are equal. Normally, one would
      // calculate the average of two values with (a + b) / 2. We use
      // a + (b - a) / 2, because it's less likely to overflow.
      int64_t local_then = message->time_check_response_->requestor_time_ +
                           (media::Timeline::local_now() -
                            message->time_check_response_->requestor_time_) /
                               2;

      // Create a function that translates remote system time to local system
      // time. We assume that both clocks run at the same rate (hence 1, 1).
      remote_to_local_ = media::TimelineFunction(
          local_then, message->time_check_response_->responder_time_, 1, 1);
    } break;

    case MediaPlayerOutMessageType::kStatusNotification:
      FXL_DCHECK(message->status_notification_);
      status_ = std::move(message->status_notification_->status_);
      if (status_->timeline_transform) {
        // Use the remote-to-local conversion established after the time check
        // transaction to translate reference time into local system time.
        status_->timeline_transform->reference_time =
            remote_to_local_(status_->timeline_transform->reference_time);
      }
      SendStatusUpdates();
      break;
  }
}

void MediaPlayerNetProxy::SendStatusUpdates() {
  for (auto& binding : bindings().bindings()) {
    binding->events().StatusChanged(fidl::Clone(*status_));
  }
}

}  // namespace media_player
