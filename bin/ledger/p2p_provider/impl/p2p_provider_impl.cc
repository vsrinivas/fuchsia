// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"

#include <algorithm>
#include <iterator>

#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/ledger/p2p_provider/impl/envelope_generated.h"
#include "peridot/lib/ledger_client/constants.h"

namespace p2p_provider {
namespace {
constexpr char kRespondingServiceName[] = "ledger-p2p-";
const uint16_t kCurrentVersion = 0;

bool ValidateHandshake(const Envelope* envelope, const Handshake** message) {
  if (envelope->message_type() != EnvelopeMessage_Handshake) {
    FXL_LOG(ERROR) << "Incorrect message type: " << envelope->message_type();
    return false;
  }
  *message = static_cast<const Handshake*>(envelope->message());
  if ((*message)->version() != kCurrentVersion) {
    FXL_LOG(ERROR) << "Incorrect message version: " << (*message)->version();
    return false;
  }
  return true;
}

}  // namespace

P2PProviderImpl::P2PProviderImpl(
    std::string host_name, fuchsia::netconnector::NetConnectorPtr net_connector,
    std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider)
    : host_name_(std::move(host_name)),
      net_connector_(std::move(net_connector)),
      user_id_provider_(std::move(user_id_provider)) {}

P2PProviderImpl::~P2PProviderImpl() {}

void P2PProviderImpl::Start(Client* client) {
  FXL_DCHECK(!client_);
  FXL_DCHECK(client);
  client_ = client;
  user_id_provider_->GetUserId(
      [this](UserIdProvider::Status status, std::string user_id) {
        if (status != UserIdProvider::Status::OK) {
          FXL_LOG(ERROR) << "Unable to retrieve the user ID necessary to start "
                            "the peer-to-peer provider.";
          return;
        }
        user_id_ = user_id;
        StartService();
      });
}

bool P2PProviderImpl::SendMessage(fxl::StringView destination,
                                  fxl::StringView data) {
  flatbuffers::FlatBufferBuilder buffer;
  flatbuffers::Offset<Message> message =
      CreateMessage(buffer, convert::ToFlatBufferVector(&buffer, data));
  flatbuffers::Offset<Envelope> envelope =
      CreateEnvelope(buffer, EnvelopeMessage_Message, message.Union());
  buffer.Finish(envelope);

  char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
  size_t size = buffer.GetSize();
  auto it = connection_map_.find(destination);
  if (it == connection_map_.end()) {
    return false;
  }
  it->second->SendMessage(fxl::StringView(buf, size));
  return true;
}

void P2PProviderImpl::StartService() {
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
  // When the service provider is reset and its connection cut, NetConnector
  // stops responding for its services.
  network_service_provider_.AddBinding(handle.NewRequest());
  network_service_provider_.AddServiceForName(
      [this](zx::channel channel) {
        RemoteConnection& connection = connections_.emplace(host_name_);
        connection.set_on_message(
            [this, &connection](std::vector<uint8_t> data) {
              ProcessHandshake(&connection, std::move(data), true,
                               fxl::StringView());
            });
        connection.Start(std::move(channel));
      },
      kRespondingServiceName + user_id_);
  net_connector_->RegisterServiceProvider(kRespondingServiceName + user_id_,
                                          std::move(handle));

  ListenForNewDevices(fuchsia::netconnector::kInitialKnownDeviceNames);
}

void P2PProviderImpl::ProcessHandshake(RemoteConnection* connection,
                                       std::vector<uint8_t> data,
                                       bool should_send_handshake,
                                       fxl::StringView network_remote_name) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyEnvelopeBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    connection->Disconnect();
    return;
  };
  const Envelope* envelope = GetEnvelope(data.data());
  const Handshake* message;
  if (!ValidateHandshake(envelope, &message)) {
    FXL_LOG(ERROR) << "The message received is not valid.";
    connection->Disconnect();
    return;
  }

  std::string remote_name(message->host_name()->begin(),
                          message->host_name()->end());
  if (!network_remote_name.empty() && network_remote_name != remote_name) {
    // The name of the remote device as given by the network is different from
    // the self-declared name. Something is wrong here, let's abort.
    FXL_LOG(ERROR) << "Network name " << network_remote_name
                   << " different from declared name " << remote_name
                   << ", aborting.";
    connection->Disconnect();
    return;
  }
  bool existed_before = false;
  auto it = connection_map_.find(remote_name);
  if (it != connection_map_.end()) {
    if (remote_name < host_name_) {
      it->second->Disconnect();
      existed_before = true;
    } else {
      connection->Disconnect();
      return;
    }
  }

  connection_map_[remote_name] = connection;

  connection->set_on_close([this, remote_name]() {
    connection_map_.erase(remote_name);
    OnDeviceChange(remote_name, DeviceChangeType::DELETED);
  });

  connection->set_on_message([this, remote_name](std::vector<uint8_t> data) {
    Dispatch(remote_name, std::move(data));
  });

  if (should_send_handshake) {
    // We send an handshake to signal to the other side the connection is
    // indeed established.
    flatbuffers::FlatBufferBuilder buffer;
    flatbuffers::Offset<Handshake> request =
        CreateHandshake(buffer, kCurrentVersion,
                        convert::ToFlatBufferVector(&buffer, host_name_));
    flatbuffers::Offset<Envelope> envelope =
        CreateEnvelope(buffer, EnvelopeMessage_Handshake, request.Union());
    buffer.Finish(envelope);
    char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
    size_t size = buffer.GetSize();
    connection->SendMessage(fxl::StringView(buf, size));
  }

  if (!existed_before) {
    // If the connection existed before, we don't need to notify again.
    OnDeviceChange(remote_name, DeviceChangeType::NEW);
  }
}

void P2PProviderImpl::ListenForNewDevices(uint64_t version) {
  net_connector_->GetKnownDeviceNames(
      version,
      [this](uint64_t new_version, fidl::VectorPtr<fidl::StringPtr> devices) {
        std::vector<std::string> seen_devices;
        for (auto& remote_name : *devices) {
          seen_devices.push_back(remote_name);
          if (contacted_hosts_.find(remote_name) != contacted_hosts_.end()) {
            continue;
          }
          if (remote_name == host_name_) {
            continue;
          }
          std::string remote_name_str(remote_name);

          zx::channel local;
          zx::channel remote;
          zx_status_t status = zx::channel::create(0u, &local, &remote);

          FXL_CHECK(status == ZX_OK)
              << "zx::channel::create failed, status " << status;

          fuchsia::sys::ServiceProviderPtr device_service_provider;
          net_connector_->GetDeviceServiceProvider(
              remote_name, device_service_provider.NewRequest());

          device_service_provider->ConnectToService(
              kRespondingServiceName + user_id_, std::move(remote));

          flatbuffers::FlatBufferBuilder buffer;
          flatbuffers::Offset<Handshake> request =
              CreateHandshake(buffer, kCurrentVersion,
                              convert::ToFlatBufferVector(&buffer, host_name_));
          flatbuffers::Offset<Envelope> envelope = CreateEnvelope(
              buffer, EnvelopeMessage_Handshake, request.Union());
          buffer.Finish(envelope);

          RemoteConnection& connection = connections_.emplace(host_name_);
          connection.set_on_message(
              [this, &connection, remote_name_str](std::vector<uint8_t> data) {
                ProcessHandshake(&connection, std::move(data), false,
                                 remote_name_str);
              });
          connection.Start(std::move(local));

          char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
          size_t size = buffer.GetSize();
          connection.SendMessage(fxl::StringView(buf, size));
          contacted_hosts_.insert(std::move(remote_name_str));
        }
        // Devices that disappeared can be recontacted again later as they might
        // have changed.
        std::vector<fxl::StringView> to_be_removed;
        std::set_difference(contacted_hosts_.begin(), contacted_hosts_.end(),
                            seen_devices.begin(), seen_devices.end(),
                            std::back_inserter(to_be_removed));
        for (const fxl::StringView& host : to_be_removed) {
          contacted_hosts_.erase(contacted_hosts_.find(host));
        }
        ListenForNewDevices(new_version);
      });
}

void P2PProviderImpl::Dispatch(fxl::StringView source,
                               std::vector<uint8_t> data) {
  FXL_DCHECK(client_);
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyEnvelopeBuffer(verifier)) {
    // Wrong serialization, abort.
    FXL_LOG(ERROR) << "The message received is malformed.";
    return;
  };
  const Envelope* envelope = GetEnvelope(data.data());
  if (envelope->message_type() != EnvelopeMessage_Message) {
    FXL_LOG(ERROR) << "The message received is unexpected at this point.";
    return;
  }

  const Message* message = static_cast<const Message*>(envelope->message());

  fxl::StringView data_view(
      reinterpret_cast<const char*>(message->data()->data()),
      message->data()->size());
  client_->OnNewMessage(source, data_view);
}

void P2PProviderImpl::OnDeviceChange(fxl::StringView remote_device,
                                     DeviceChangeType change_type) {
  FXL_DCHECK(client_);
  client_->OnDeviceChange(remote_device, change_type);
}

}  // namespace p2p_provider
