// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/overnet/fake_overnet.h"

#include <fuchsia/overnet/cpp/fidl.h>

#include "lib/async/dispatcher.h"

namespace ledger {

FakeOvernet::ServiceProviderHolder::ServiceProviderHolder(
    fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> handle)
    : ptr_(handle.Bind()) {}

void FakeOvernet::ServiceProviderHolder::SetOnDiscardable(fit::closure on_discardable) {
  ptr_.set_error_handler(
      [on_discardable = std::move(on_discardable)](zx_status_t /* status */) { on_discardable(); });
}

bool FakeOvernet::ServiceProviderHolder::IsDiscardable() const { return ptr_.is_bound(); }

fuchsia::overnet::ServiceProvider* FakeOvernet::ServiceProviderHolder::operator->() const {
  return ptr_.get();
}

fuchsia::overnet::ServiceProvider& FakeOvernet::ServiceProviderHolder::operator*() const {
  return *ptr_.get();
}

FakeOvernet::FakeOvernet(async_dispatcher_t* dispatcher, uint64_t self_id, Delegate* delegate)
    : self_id_(self_id), delegate_(delegate), service_providers_(dispatcher) {}

void FakeOvernet::GetService(std::string service_name, zx::channel chan) {
  auto it = service_providers_.find(service_name);
  if (it == service_providers_.end()) {
    return;
  }
  it->second->ConnectToService(std::move(chan));
}

std::vector<std::string> FakeOvernet::GetAllServices() const {
  std::vector<std::string> services;
  for (auto& it : service_providers_) {
    services.push_back(it.first);
  }
  return services;
}

void FakeOvernet::ConnectToService(fuchsia::overnet::protocol::NodeId node,
                                   std::string service_name, zx::channel channel) {
  delegate_->ConnectToService(std::move(node), std::move(service_name), std::move(channel));
}

void FakeOvernet::RegisterService(
    std::string name, fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> service_provider) {
  service_providers_.emplace(name, std::move(service_provider));
  delegate_->ServiceWasRegistered();
}

void FakeOvernet::ListPeers(uint64_t version_last_seen, ListPeersCallback callback) {
  delegate_->ListPeers(version_last_seen,
                       [callback = std::move(callback), self_id = self_id_](
                           uint64_t version, std::vector<Delegate::FakePeer> fake_peers) {
                         std::vector<fuchsia::overnet::Peer> overnet_peers;
                         for (auto& fake_peer : fake_peers) {
                           fuchsia::overnet::Peer& overnet_peer = overnet_peers.emplace_back();

                           // Set the id and is_self.
                           overnet_peer.id = std::move(fake_peer.id);
                           if (overnet_peer.id.id == self_id) {
                             overnet_peer.is_self = true;
                           } else {
                             overnet_peer.is_self = false;
                           }

                           // Set the description.
                           fuchsia::overnet::protocol::PeerDescription description;
                           description.set_services(fake_peer.services);
                           overnet_peer.description = std::move(description);
                         }
                         callback(version, std::move(overnet_peers));
                       });
}

}  // namespace ledger
