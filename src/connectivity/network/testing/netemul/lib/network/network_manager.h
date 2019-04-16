// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_MANAGER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_MANAGER_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "src/connectivity/network/testing/netemul/lib/network/network.h"

namespace netemul {
class NetworkContext;
class NetworkManager : public fuchsia::netemul::network::NetworkManager {
 public:
  using FNetworkManager = fuchsia::netemul::network::NetworkManager;

  explicit NetworkManager(NetworkContext* context);

  // create network
  zx_status_t CreateNetwork(std::string name, Network::Config config,
                            fidl::InterfaceRequest<Network::FNetwork> req);
  // Gets a network with name
  Network* GetNetwork(const std::string& name);

  // fidl interface implementations:
  void ListNetworks(ListNetworksCallback callback) override;
  void CreateNetwork(std::string name, Network::Config config,
                     CreateNetworkCallback callback) override;
  void GetNetwork(::std::string name, GetNetworkCallback callback) override;

  void Bind(fidl::InterfaceRequest<FNetworkManager> request);

 private:
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  fidl::BindingSet<FNetworkManager> bindings_;
  std::unordered_map<std::string, Network::Ptr> nets_;
};
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_MANAGER_H_
