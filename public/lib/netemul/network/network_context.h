// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_NETWORK_CONTEXT_H_
#define LIB_NETEMUL_NETWORK_NETWORK_CONTEXT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include "lib/netemul/network/endpoint_manager.h"
#include "lib/netemul/network/network_manager.h"

namespace netemul {

class NetworkContext : public fuchsia::netemul::network::NetworkContext {
 public:
  using FNetworkContext = fuchsia::netemul::network::NetworkContext;
  explicit NetworkContext(async_dispatcher_t* dispatcher = nullptr);

  async_dispatcher_t* dispatcher() { return dispatcher_; }

  NetworkManager& network_manager() { return network_manager_; }

  EndpointManager& endpoint_manager() { return endpoint_manager_; }

  void GetNetworkManager(
      ::fidl::InterfaceRequest<NetworkManager::FNetworkManager> net_manager)
      override;
  void GetEndpointManager(
      fidl::InterfaceRequest<EndpointManager::FEndpointManager> endp_manager)
      override;

  fidl::InterfaceRequestHandler<FNetworkContext> GetHandler();

 private:
  async_dispatcher_t* dispatcher_;
  NetworkManager network_manager_;
  EndpointManager endpoint_manager_;
  fidl::BindingSet<FNetworkContext> bindings_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_NETWORK_CONTEXT_H_
