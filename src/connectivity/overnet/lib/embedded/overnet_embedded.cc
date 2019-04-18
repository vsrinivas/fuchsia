// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/overnet_embedded.h"
#include <random>

namespace overnet {

OvernetEmbedded::OvernetEmbedded()
    : shutdown_([this] {
        bool done = false;
        endpoint_.Close([&done] { done = true; });
        while (!done) {
          OVERNET_TRACE(INFO) << "Waiting to exit";
          reactor_.Step();
        }
      }) {}

OvernetEmbedded::~OvernetEmbedded() = default;

NodeId OvernetEmbedded::GenerateNodeId() {
  std::random_device rng_dev;
  std::uniform_int_distribution<uint64_t> distrib;
  return NodeId(distrib(rng_dev));
}

void OvernetEmbedded::ListPeers(uint64_t last_seen_version,
                                ListPeersCallback callback) {
  endpoint_.OnNodeDescriptionTableChange(
      last_seen_version,
      Callback<void>(
          ALLOCATED_CALLBACK, [this, callback = std::move(callback)] {
            std::vector<fuchsia::overnet::embedded::Peer> response;
            auto new_version = endpoint_.ForEachNodeDescription(
                [&response, self_node = endpoint_.node_id()](
                    overnet::NodeId id,
                    const fuchsia::overnet::protocol::PeerDescription& m) {
                  fuchsia::overnet::embedded::Peer peer;
                  peer.id = fidl::ToEmbedded(id.as_fidl());
                  peer.is_self = id == self_node;
                  peer.description = fidl::ToEmbedded(m);
                  response.emplace_back(std::move(peer));
                });
            callback(new_version, std::move(response));
          }));
}

void OvernetEmbedded::RegisterService(
    std::string service_name,
    std::unique_ptr<fuchsia::overnet::embedded::ServiceProvider_Proxy>
        service_provider) {
  services_.emplace(std::move(service_name), std::move(service_provider));
}

void OvernetEmbedded::ConnectToService(
    fuchsia::overnet::protocol::embedded::NodeId node, std::string service_name,
    ClosedPtr<ZxChannel> channel) {
  if (node == fidl::ToEmbedded(endpoint_.node_id().as_fidl())) {
    auto it = services_.find(service_name);
    if (it != services_.end()) {
      it->second->ConnectToService(std::move(channel));
    } else {
      OVERNET_TRACE(ERROR) << "Service not found: " << service_name;
    }
    return;
  }
  auto new_stream = endpoint_.InitiateStream(
      NodeId(node.id),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      service_name);
  if (new_stream.is_error()) {
    OVERNET_TRACE(ERROR) << "Failed to create stream to initiate service: "
                         << service_name;
    return;
  }
  channel->Bind(std::move(*new_stream));
}

OvernetEmbedded::Actor::Actor(OvernetEmbedded* root) : root_(root) {
  assert(root->actors_);
  root->actors_->push_back(this);
}

OvernetEmbedded::Actor::~Actor() {
  if (root_->actors_) {
    root_->actors_->erase(
        std::remove(root_->actors_->begin(), root_->actors_->end(), this));
  }
}

int OvernetEmbedded::Run() {
  auto shutdown = std::move(shutdown_);
  ZX_ASSERT(!shutdown.empty());

  {
    assert(actors_);
    auto actors = std::move(actors_);
    for (Actor* actor : *actors) {
      if (auto status = actor->Start(); status.is_error()) {
        OVERNET_TRACE(ERROR) << "Failed to start actor: " << status;
        return 1;
      }
    }
  }

  if (auto status = reactor_.Run(); status.is_error()) {
    OVERNET_TRACE(ERROR) << "Run loop failed: " << status;
    return 1;
  }
  return 0;
}

}  // namespace overnet
