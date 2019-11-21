// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"

#include <algorithm>
#include <iterator>

#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace p2p_provider {
namespace {
// Prefix for the peer-to-peer service.
constexpr char kRespondingServiceName[] = "ledger-p2p";
// Separator for the different parts of the service name.
constexpr char kRespondingServiceNameSeparator[] = "/";
// Current Ledger protocol version. Devices on different versions are unable to talk to each other.
const uint16_t kCurrentVersion = 1;

}  // namespace

P2PProviderImpl::P2PProviderImpl(fuchsia::overnet::OvernetPtr overnet,
                                 std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider,
                                 rng::Random* random)
    : service_binding_(this),
      overnet_(std::move(overnet)),
      user_id_provider_(std::move(user_id_provider)),
      random_(random) {}

P2PProviderImpl::~P2PProviderImpl() = default;

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

bool P2PProviderImpl::SendMessage(const P2PClientId& destination,
                                  convert::ExtendedStringView data) {
  auto it = connections_.find(destination);
  if (it == connections_.end()) {
    return false;
  }
  it->second.SendMessage(data);
  return true;
}

void P2PProviderImpl::StartService() {
  fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> handle;
  service_binding_.Bind(handle.NewRequest());
  overnet_->PublishService(OvernetServiceName(), std::move(handle));
  ListenForNewDevices();
}

void P2PProviderImpl::ConnectToService(zx::channel chan,
                                       fuchsia::overnet::ConnectionInfo connection_info) {
  AddConnectionFromChannel(std::move(chan), std::nullopt);
}

void P2PProviderImpl::AddConnectionFromChannel(
    zx::channel chan, std::optional<fuchsia::overnet::protocol::NodeId> overnet_id) {
  if (overnet_id) {
    FXL_DCHECK(contacted_peers_.find(*overnet_id) == contacted_peers_.end())
        << "Connecting to an already contacted peer.";
    contacted_peers_.emplace(*overnet_id);
  }

  p2p_provider::P2PClientId id = MakeRandomP2PClientId(random_);
  auto& connection = connections_[id];

  connection.set_on_close([this, id, overnet_id]() {
    connections_.erase(id);
    if (overnet_id) {
      contacted_peers_.erase(*overnet_id);
    }
    OnDeviceChange(id, DeviceChangeType::DELETED);
  });

  connection.set_on_message(
      [this, id](std::vector<uint8_t> data) { Dispatch(id, std::move(data)); });

  connection.Start(std::move(chan));
  OnDeviceChange(id, DeviceChangeType::NEW);
}

void P2PProviderImpl::ListenForNewDevices() {
  overnet_->ListPeers([this](std::vector<fuchsia::overnet::Peer> peers) {
    if (!self_client_id_) {
      // We are starting and we don't know who we are yet. Let's find out
      // first so we can connect to peers correctly.
      for (auto& peer : peers) {
        if (!peer.is_self) {
          continue;
        }
        self_client_id_ = peer.id;
        break;
      }
    }
    if (!self_client_id_) {
      ListenForNewDevices();
      return;
    }
    for (auto& peer : peers) {
      if (peer.is_self) {
        continue;
      }
      if (peer.id.id < self_client_id_->id) {
        // The other side will connect to us, no need to duplicate
        // connections.
        continue;
      }

      if (contacted_peers_.find(peer.id) != contacted_peers_.end()) {
        // Already connected to the peer.
        continue;
      }

      const fuchsia::overnet::protocol::PeerDescription& description = peer.description;
      if (!description.has_services()) {
        continue;
      }
      const std::vector<std::string>& services = description.services();
      bool ledger_service_is_present_on_other_side =
          std::find(services.begin(), services.end(), OvernetServiceName()) != services.end();
      if (!ledger_service_is_present_on_other_side) {
        continue;
      }

      zx::channel local;
      zx::channel remote;
      zx_status_t status = zx::channel::create(0u, &local, &remote);
      FXL_CHECK(status == ZX_OK) << "zx::channel::create failed, status " << status;
      overnet_->ConnectToService(peer.id, OvernetServiceName(), std::move(remote));
      AddConnectionFromChannel(std::move(local), peer.id);
    }
    ListenForNewDevices();
  });
}

void P2PProviderImpl::Dispatch(P2PClientId source, std::vector<uint8_t> data) {
  FXL_DCHECK(client_);
  client_->OnNewMessage(source, data);
}

void P2PProviderImpl::OnDeviceChange(P2PClientId remote_device, DeviceChangeType change_type) {
  FXL_DCHECK(client_);
  client_->OnDeviceChange(remote_device, change_type);
}

std::string P2PProviderImpl::OvernetServiceName() {
  return std::string(kRespondingServiceName) + kRespondingServiceNameSeparator +
         std::to_string(kCurrentVersion) + kRespondingServiceNameSeparator + user_id_;
}

}  // namespace p2p_provider
