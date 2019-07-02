// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/overnet/fake_overnet.h"

#include <fuchsia/overnet/cpp/fidl.h>

namespace ledger {

FakeOvernet::ServiceProviderHolder::ServiceProviderHolder(
    fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> handle)
    : ptr_(handle.Bind()) {}

void FakeOvernet::ServiceProviderHolder::set_on_empty(fit::closure on_empty) {
  ptr_.set_error_handler(
      [on_empty = std::move(on_empty)](zx_status_t /* status */) { on_empty(); });
}

fuchsia::overnet::ServiceProvider* FakeOvernet::ServiceProviderHolder::operator->() const {
  return ptr_.get();
}

fuchsia::overnet::ServiceProvider& FakeOvernet::ServiceProviderHolder::operator*() const {
  return *ptr_.get();
}

FakeOvernet::FakeOvernet(uint64_t self_id, Delegate* delegate)
    : self_id_(self_id), delegate_(delegate) {}

void FakeOvernet::GetService(std::string service_name, zx::channel chan) {
  auto it = service_providers_.find(service_name);
  if (it == service_providers_.end()) {
    return;
  }
  it->second->ConnectToService(std::move(chan));
}

void FakeOvernet::ConnectToService(fuchsia::overnet::protocol::NodeId node,
                                   std::string service_name, zx::channel channel) {
  delegate_->ConnectToService(std::move(node), std::move(service_name), std::move(channel));
}

void FakeOvernet::RegisterService(
    std::string name, fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> service_provider) {
  service_providers_.emplace(name, std::move(service_provider));
}

void FakeOvernet::ListPeers(uint64_t version_last_seen, ListPeersCallback callback) {
  delegate_->ListPeers(
      version_last_seen,
      [callback = std::move(callback), self_id = self_id_](
          uint64_t version, std::vector<fuchsia::overnet::protocol::NodeId> nodes) {
        std::vector<fuchsia::overnet::Peer> peers;
        for (auto& node : nodes) {
          fuchsia::overnet::Peer& peer = peers.emplace_back();
          peer.id = std::move(node);
          if (peer.id.id == self_id) {
            peer.is_self = true;
          } else {
            peer.is_self = false;
          }
        }
        callback(version, std::move(peers));
      });
}

}  // namespace ledger
