// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/netconnector/netconnector_example/netconnector_example_impl.h"

#include <lib/async/default.h>
#include <lib/zx/channel.h>
#include <fuchsia/netconnector/cpp/fidl.h>

#include "garnet/examples/netconnector/netconnector_example/netconnector_example_params.h"
#include "lib/fxl/logging.h"

namespace examples {
namespace {
static constexpr char kRespondingServiceName[] = "netconnector::Example";

static const std::vector<std::string> kConversation = {
    "Hello!",    "Hello!",   "Do you like my hat?",
    "I do not.", "Good-by!", "Good-by!"};
}  // namespace

NetConnectorExampleImpl::NetConnectorExampleImpl(
    NetConnectorExampleParams* params, fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)),
      startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  // The MessageRelay makes using the channel easier. Hook up its callbacks.
  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  // Quit when the local channel closes, unless we're registering our provider.
  // In that case, we need to stay around to respond to future requests.
  if (params->register_provider()) {
    message_relay_.SetChannelClosedCallback([this]() {
      if (conversation_iter_ == kConversation.end()) {
        FXL_LOG(INFO) << "Channel closed, quitting";
      } else {
        FXL_LOG(ERROR) << "Channel closed unexpectedly, quitting";
      }

      quit_callback_();
    });
  }

  // Start at the beginning of the conversation. The party that receives the
  // last message in the conversation closes the channel.
  conversation_iter_ = kConversation.begin();

  if (params->request_device_name().empty()) {
    // Params say we should be responding. Register the responding service.
    FXL_LOG(INFO) << "Running as responder";
    startup_context_->outgoing_services()->AddServiceForName(
        [this](zx::channel channel) {
          message_relay_.SetChannel(std::move(channel));
        },
        kRespondingServiceName);

    if (params->register_provider()) {
      // Register our provider with netconnector.
      FXL_LOG(INFO) << "Registering provider";
      fuchsia::netconnector::NetConnectorPtr connector =
          startup_context_
              ->ConnectToEnvironmentService<fuchsia::netconnector::NetConnector>();

      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
      startup_context_->outgoing_services()->AddBinding(handle.NewRequest());

      FXL_DCHECK(handle);

      connector->RegisterServiceProvider(kRespondingServiceName,
                                         std::move(handle));
    }
  } else {
    // Params say we should be a requestor.
    FXL_LOG(INFO) << "Running as requestor";
    fuchsia::netconnector::NetConnectorPtr connector =
        startup_context_
            ->ConnectToEnvironmentService<fuchsia::netconnector::NetConnector>();

    // Create a pair of channels.
    zx::channel local;
    zx::channel remote;
    zx_status_t status = zx::channel::create(0u, &local, &remote);

    FXL_CHECK(status == ZX_OK)
        << "zx::channel::create failed, status " << status;

    // Give the local end of the channel to the relay.
    message_relay_.SetChannel(std::move(local));

    // Pass the remote end to NetConnector.
    fuchsia::sys::ServiceProviderPtr device_service_provider;
    connector->GetDeviceServiceProvider(params->request_device_name(),
                                        device_service_provider.NewRequest());

    device_service_provider->ConnectToService(kRespondingServiceName,
                                              std::move(remote));

    // Start the conversation.
    SendMessage(*conversation_iter_);
    ++conversation_iter_;
    FXL_DCHECK(conversation_iter_ != kConversation.end());
  }
}

NetConnectorExampleImpl::~NetConnectorExampleImpl() {}

void NetConnectorExampleImpl::SendMessage(const std::string& message_string) {
  FXL_LOG(INFO) << "Sending message: '" << message_string << "'";

  std::vector<uint8_t> message(message_string.size());
  std::memcpy(message.data(), message_string.data(), message.size());

  message_relay_.SendMessage(std::move(message));
}

void NetConnectorExampleImpl::HandleReceivedMessage(
    std::vector<uint8_t> message) {
  std::string message_string(reinterpret_cast<char*>(message.data()), 0,
                             message.size());

  FXL_LOG(INFO) << "Message received: '" << message_string << "'";

  if (conversation_iter_ == kConversation.end()) {
    FXL_LOG(ERROR) << "Expected the channel to close, closing channel";
    message_relay_.CloseChannel();
    return;
  }

  if (message_string != *conversation_iter_) {
    FXL_LOG(ERROR) << "Expected '" << *conversation_iter_
                   << "', closing channel";
    message_relay_.CloseChannel();
    return;
  }

  ++conversation_iter_;
  if (conversation_iter_ == kConversation.end()) {
    FXL_LOG(INFO) << "Conversation complete, closing channel";
    message_relay_.CloseChannel();
    return;
  }

  SendMessage(*conversation_iter_);
  ++conversation_iter_;
  // We may have hit the end of the conversation here, but if so, the remote
  // party is expected to close the channel.
}

}  // namespace examples
