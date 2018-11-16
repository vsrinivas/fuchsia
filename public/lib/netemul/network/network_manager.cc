// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_manager.h"
#include "network_context.h"

namespace netemul {

NetworkManager::NetworkManager(NetworkContext* context) : parent_(context) {}

void NetworkManager::ListNetworks(
    NetworkManager::ListNetworksCallback callback) {
  auto rsp = fidl::VectorPtr<fidl::StringPtr>::New(0);
  rsp->reserve(nets_.size());
  for (auto& x : nets_) {
    rsp->push_back(x.first);
  }
  callback(std::move(rsp));
}

void NetworkManager::CreateNetwork(
    ::fidl::StringPtr name, fuchsia::netemul::network::NetworkConfig config,
    NetworkManager::CreateNetworkCallback callback) {
  if (name->empty()) {
    // empty name not allowed
    callback(ZX_ERR_INVALID_ARGS, fidl::InterfaceHandle<Network::FNetwork>());
  } else if (nets_.find(name) == nets_.end()) {  // no network with same name
    // create new network
    auto net = std::make_unique<Network>(parent_, name, std::move(config));
    auto binding = net->Bind();

    net->SetClosedCallback([this](const Network& net) {
      // erase network when all its bindings go away
      nets_.erase(net.name());
    });

    nets_.insert(std::make_pair<std::string, Network::Ptr>(std::string(name),
                                                           std::move(net)));

    callback(ZX_OK, std::move(binding));
  } else {
    // name already exists, decline creation
    callback(ZX_ERR_ALREADY_EXISTS, fidl::InterfaceHandle<Network::FNetwork>());
  }
}

void NetworkManager::GetNetwork(::fidl::StringPtr name,
                                NetworkManager::GetNetworkCallback callback) {
  auto neti = nets_.find(name);
  if (neti == nets_.end()) {
    // no network with such name
    callback(fidl::InterfaceHandle<Network::FNetwork>());
  } else {
    auto& net = neti->second;
    callback(net->Bind());
  }
}

void NetworkManager::Bind(fidl::InterfaceRequest<FNetworkManager> request) {
  bindings_.AddBinding(this, std::move(request), parent_->dispatcher());
}

}  // namespace netemul
