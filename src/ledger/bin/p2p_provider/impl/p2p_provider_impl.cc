// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"

#include <algorithm>
#include <iterator>

#include "src/ledger/bin/p2p_provider/impl/envelope_generated.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace p2p_provider {
namespace {
// Prefix for the peer-to-peer service.
constexpr char kRespondingServiceName[] = "ledger-p2p-";
// Current Ledger protocol version. Devices on different versions are unable to talk to each other.
const uint16_t kCurrentVersion = 1;
// Initial version of the list of peers, when starting Overnet.
const uint64_t kInitialOvernetVersion = 0;

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

P2PProviderImpl::P2PProviderImpl(fuchsia::overnet::OvernetPtr overnet,
                                 std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider)
    : service_binding_(this),
      overnet_(std::move(overnet)),
      user_id_provider_(std::move(user_id_provider)) {}

P2PProviderImpl::~P2PProviderImpl() {}

void P2PProviderImpl::Start(Client* client) {
  FXL_DCHECK(!client_);
  FXL_DCHECK(client);
  client_ = client;
  user_id_provider_->GetUserId([this](UserIdProvider::Status status, std::string user_id) {
    if (status != UserIdProvider::Status::OK) {
      FXL_LOG(ERROR) << "Unable to retrieve the user ID necessary to start "
                        "the peer-to-peer provider.";
      return;
    }
    user_id_ = user_id;
    StartService();
  });
}

bool P2PProviderImpl::SendMessage(const P2PClientId& destination, fxl::StringView data) {
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
  fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> handle;
  service_binding_.Bind(handle.NewRequest());
  overnet_->RegisterService(kRespondingServiceName + user_id_, std::move(handle));
  ListenForNewDevices(kInitialOvernetVersion);
}

void P2PProviderImpl::ConnectToService(zx::channel chan) {
  if (!self_client_id_) {
    // We don't know who we are yet, so we won't be able to handshake properly.
    // Let's wait until we have more information.
    pending_requests_.push_back(std::move(chan));
    return;
  }
  RemoteConnection& connection = connections_.emplace();
  connection.set_on_message([this, &connection](std::vector<uint8_t> data) {
    ProcessHandshake(&connection, std::move(data), true, std::optional<P2PClientId>());
  });
  connection.Start(std::move(chan));
}

void P2PProviderImpl::ProcessHandshake(RemoteConnection* connection, std::vector<uint8_t> data,
                                       bool should_send_handshake,
                                       std::optional<P2PClientId> network_remote_node) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
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

  P2PClientId remote_node = P2PClientId(convert::ToArray(message->client_id()));

  if (network_remote_node && *network_remote_node != remote_node) {
    // The name of the remote device as given by the network is different from
    // the self-declared name. Something is wrong here, let's abort.
    FXL_LOG(ERROR) << "Network name " << *network_remote_node << " different from declared name "
                   << remote_node << ", aborting.";
    connection->Disconnect();
    return;
  }
  bool existed_before = false;
  auto it = connection_map_.find(remote_node);
  if (it != connection_map_.end()) {
    if (remote_node < *self_client_id_) {
      it->second->Disconnect();
      existed_before = true;
    } else {
      connection->Disconnect();
      return;
    }
  }

  connection_map_[remote_node] = connection;

  connection->set_on_message(
      [this, remote_node](std::vector<uint8_t> data) { Dispatch(remote_node, std::move(data)); });

  if (should_send_handshake) {
    // We send an handshake to signal to the other side the connection is
    // indeed established.
    flatbuffers::FlatBufferBuilder buffer;
    flatbuffers::Offset<Handshake> request = CreateHandshake(
        buffer, kCurrentVersion, convert::ToFlatBufferVector(&buffer, self_client_id_->GetData()));
    flatbuffers::Offset<Envelope> envelope =
        CreateEnvelope(buffer, EnvelopeMessage_Handshake, request.Union());
    buffer.Finish(envelope);
    char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
    size_t size = buffer.GetSize();
    connection->SendMessage(fxl::StringView(buf, size));
  }

  if (!existed_before) {
    // If the connection existed before, we don't need to notify again.
    OnDeviceChange(remote_node, DeviceChangeType::NEW);
  }

  connection->set_on_close([this, remote_node]() {
    connection_map_.erase(remote_node);
    OnDeviceChange(remote_node, DeviceChangeType::DELETED);
  });
}

void P2PProviderImpl::ListenForNewDevices(uint64_t version) {
  overnet_->ListPeers(version, [this](uint64_t new_version,
                                      std::vector<fuchsia::overnet::Peer> peers) {
    if (!self_client_id_) {
      // We are starting and we don't know who we are yet. Let's find out
      // first so we can connect and respond to peers correctly.
      for (auto& peer : peers) {
        if (!peer.is_self) {
          continue;
        }
        self_client_id_ = MakeP2PClientId(peer.id);
        for (zx::channel& chan : pending_requests_) {
          ConnectToService(std::move(chan));
        }
        pending_requests_.clear();
        break;
      }
    }
    FXL_DCHECK(self_client_id_);
    std::vector<P2PClientId> seen_devices;
    for (auto& peer : peers) {
      auto client_id = MakeP2PClientId(peer.id);
      seen_devices.push_back(client_id);
      if (contacted_hosts_.find(client_id) != contacted_hosts_.end()) {
        continue;
      }
      if (peer.is_self) {
        continue;
      }
      if (client_id < *self_client_id_) {
        // The other side will connect to us, no need to duplicate
        // connections.
        continue;
      }

      zx::channel local;
      zx::channel remote;
      zx_status_t status = zx::channel::create(0u, &local, &remote);

      FXL_CHECK(status == ZX_OK) << "zx::channel::create failed, status " << status;

      overnet_->ConnectToService(peer.id, kRespondingServiceName + user_id_, std::move(remote));

      flatbuffers::FlatBufferBuilder buffer;
      flatbuffers::Offset<Handshake> request =
          CreateHandshake(buffer, kCurrentVersion,
                          convert::ToFlatBufferVector(&buffer, self_client_id_->GetData()));
      flatbuffers::Offset<Envelope> envelope =
          CreateEnvelope(buffer, EnvelopeMessage_Handshake, request.Union());
      buffer.Finish(envelope);

      RemoteConnection& connection = connections_.emplace();
      connection.set_on_message([this, &connection, node_id = peer.id](std::vector<uint8_t> data) {
        ProcessHandshake(&connection, std::move(data), false, MakeP2PClientId(node_id));
      });
      connection.Start(std::move(local));

      char* buf = reinterpret_cast<char*>(buffer.GetBufferPointer());
      size_t size = buffer.GetSize();
      connection.SendMessage(fxl::StringView(buf, size));
      contacted_hosts_.insert(MakeP2PClientId(peer.id));
    }
    // Devices that disappeared can be recontacted again later as they might
    // have changed.
    std::vector<P2PClientId> to_be_removed;
    std::set_difference(contacted_hosts_.begin(), contacted_hosts_.end(), seen_devices.begin(),
                        seen_devices.end(), std::back_inserter(to_be_removed));
    for (const P2PClientId& host : to_be_removed) {
      contacted_hosts_.erase(contacted_hosts_.find(host));
    }
    ListenForNewDevices(new_version);
  });
}

void P2PProviderImpl::Dispatch(P2PClientId source, std::vector<uint8_t> data) {
  FXL_DCHECK(client_);
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
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

  fxl::StringView data_view(reinterpret_cast<const char*>(message->data()->data()),
                            message->data()->size());
  client_->OnNewMessage(source, data_view);
}

void P2PProviderImpl::OnDeviceChange(P2PClientId remote_device, DeviceChangeType change_type) {
  FXL_DCHECK(client_);
  client_->OnDeviceChange(remote_device, change_type);
}

}  // namespace p2p_provider
