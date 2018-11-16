// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_NETWORK_MANAGER_H_
#define LIB_NETEMUL_NETWORK_NETWORK_MANAGER_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include "lib/netemul/network/network.h"

#include <unordered_map>

namespace netemul {
class NetworkContext;
class NetworkManager : public fuchsia::netemul::network::NetworkManager {
 public:
  using FNetworkManager = fuchsia::netemul::network::NetworkManager;

  explicit NetworkManager(NetworkContext* context);

  // fidl interface implementations:
  void ListNetworks(ListNetworksCallback callback) override;
  void CreateNetwork(::fidl::StringPtr name,
                     fuchsia::netemul::network::NetworkConfig config,
                     CreateNetworkCallback callback) override;
  void GetNetwork(::fidl::StringPtr name, GetNetworkCallback callback) override;

  void Bind(fidl::InterfaceRequest<FNetworkManager> request);

 private:
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  fidl::BindingSet<FNetworkManager> bindings_;
  std::unordered_map<std::string, Network::Ptr> nets_;
};
}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_NETWORK_MANAGER_H_
