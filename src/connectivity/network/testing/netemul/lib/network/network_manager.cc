// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_manager.h"

#include "network_context.h"

namespace netemul {

NetworkManager::NetworkManager(NetworkContext* context) : parent_(context) {}

void NetworkManager::ListNetworks(
    NetworkManager::ListNetworksCallback callback) {
  std::vector<std::string> rsp;
  rsp.reserve(nets_.size());
  for (auto& x : nets_) {
    rsp.push_back(x.first);
  }
  callback(std::move(rsp));
}

zx_status_t NetworkManager::CreateNetwork(
    std::string name, netemul::Network::Config config,
    fidl::InterfaceRequest<netemul::Network::FNetwork> req) {
  if (name.empty() || !Network::CheckConfig(config)) {
    // empty name not allowed
    // invalid config will cause invalid args.
    return ZX_ERR_INVALID_ARGS;
  } else if (nets_.find(name) == nets_.end()) {  // no network with same name
    // create new network
    auto net = std::make_unique<Network>(parent_, name, std::move(config));
    net->Bind(std::move(req));

    net->SetClosedCallback([this](const Network& net) {
      // erase network when all its bindings go away
      nets_.erase(net.name());
    });

    nets_.insert(std::make_pair<std::string, Network::Ptr>(std::string(name),
                                                           std::move(net)));

    return ZX_OK;
  } else {
    // name already exists, decline creation
    return ZX_ERR_ALREADY_EXISTS;
  }
}

void NetworkManager::CreateNetwork(
    std::string name, Network::Config config,
    NetworkManager::CreateNetworkCallback callback) {
  fidl::InterfaceHandle<Network::FNetwork> handle;
  auto status =
      CreateNetwork(std::move(name), std::move(config), handle.NewRequest());
  if (status != ZX_OK) {
    handle.TakeChannel();  // dispose of channel
  }
  callback(status, std::move(handle));
}

Network* NetworkManager::GetNetwork(const std::string& name) {
  auto neti = nets_.find(name);
  if (neti == nets_.end()) {
    return nullptr;
  } else {
    return neti->second.get();
  }
}

void NetworkManager::GetNetwork(::std::string name,
                                NetworkManager::GetNetworkCallback callback) {
  auto neti = nets_.find(name);
  fidl::InterfaceHandle<Network::FNetwork> handle;
  if (neti == nets_.end()) {
    // no network with such name
    callback(std::move(handle));
  } else {
    auto& net = neti->second;
    net->Bind(handle.NewRequest());
    callback(std::move(handle));
  }
}

void NetworkManager::Bind(fidl::InterfaceRequest<FNetworkManager> request) {
  bindings_.AddBinding(this, std::move(request), parent_->dispatcher());
}

}  // namespace netemul
