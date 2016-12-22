// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/examples/netconnector_example/netconnector_example_impl.h"

#include <mx/channel.h>

#include "apps/netconnector/examples/netconnector_example/netconnector_example_params.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace examples {
namespace {
static constexpr char kRespondingServiceName[] = "netconnector::Example";

static const std::vector<std::string> kConversation = {
    "Hello!",    "Hello!",   "Do you like my hat?",
    "I do not.", "Good-by!", "Good-by!"};
}  // namespace

NetConnectorExampleImpl::NetConnectorExampleImpl(
    NetConnectorExampleParams* params)
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()) {
  // The MessageRelay makes using the channel easier. Hook up its callbacks.
  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) { HandleReceivedMessage(message); });

  message_relay_.SetChannelClosedCallback([this]() {
    if (conversation_iter_ == kConversation.end()) {
      FTL_LOG(INFO) << "Channel closed, quitting";
    } else {
      FTL_LOG(ERROR) << "Channel closed unexpectedly, quitting";
    }

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  // Start at the beginning of the conversation. The party that receives the
  // last message in the conversation closes the channel.
  conversation_iter_ = kConversation.begin();

  if (params->request_device_name().empty()) {
    // Params say we should be responding. Register the responding service.
    application_context_->outgoing_services()->AddServiceForName(
        [this](mx::channel channel) {
          message_relay_.SetChannel(std::move(channel));
        },
        kRespondingServiceName);
  } else {
    // Params say we should be a requestor.
    netconnector::NetConnectorPtr connector =
        application_context_
            ->ConnectToEnvironmentService<netconnector::NetConnector>();

    // Create a pair of channels.
    mx::channel local;
    mx::channel remote;
    mx_status_t status = mx::channel::create(0u, &local, &remote);

    FTL_CHECK(status == NO_ERROR) << "mx::channel::create failed, status "
                                  << status;

    // Give the local end of the channel to the relay.
    message_relay_.SetChannel(std::move(local));

    // Pass the remote end to NetConnector.
    modular::ServiceProviderPtr device_service_provider;
    connector->GetDeviceServiceProvider(params->request_device_name(),
                                        device_service_provider.NewRequest());

    device_service_provider->ConnectToService(kRespondingServiceName,
                                              std::move(remote));

    // Start the conversation.
    SendMessage(*conversation_iter_);
    ++conversation_iter_;
    FTL_DCHECK(conversation_iter_ != kConversation.end());
  }
}

NetConnectorExampleImpl::~NetConnectorExampleImpl() {}

void NetConnectorExampleImpl::SendMessage(const std::string& message_string) {
  FTL_LOG(INFO) << "Sending message: '" << message_string << "'";

  std::vector<uint8_t> message(message_string.size());
  std::memcpy(message.data(), message_string.data(), message.size());

  message_relay_.SendMessage(std::move(message));
}

void NetConnectorExampleImpl::HandleReceivedMessage(
    std::vector<uint8_t> message) {
  std::string message_string(reinterpret_cast<char*>(message.data()), 0,
                             message.size());

  FTL_LOG(INFO) << "Message received: '" << message_string << "'";

  if (conversation_iter_ == kConversation.end()) {
    FTL_LOG(ERROR) << "Expected the channel to close, closing channel";
    message_relay_.CloseChannel();
    return;
  }

  if (message_string != *conversation_iter_) {
    FTL_LOG(ERROR) << "Expected '" << *conversation_iter_
                   << "', closing channel";
    message_relay_.CloseChannel();
    return;
  }

  ++conversation_iter_;
  if (conversation_iter_ == kConversation.end()) {
    FTL_LOG(INFO) << "Conversation complete, closing channel";
    message_relay_.CloseChannel();
    return;
  }

  SendMessage(*conversation_iter_);
  ++conversation_iter_;
  // We may have hit the end of the conversation here, but if so, the remote
  // party is expected to close the channel.
}

}  // namespace examples
