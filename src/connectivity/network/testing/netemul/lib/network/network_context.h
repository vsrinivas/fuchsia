// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_CONTEXT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_CONTEXT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>

#include "src/connectivity/network/testing/netemul/lib/network/endpoint_manager.h"
#include "src/connectivity/network/testing/netemul/lib/network/network_manager.h"

namespace netemul {

class SetupHandle;
class NetworkContext : public fuchsia::netemul::network::NetworkContext {
 public:
  using FNetworkContext = fuchsia::netemul::network::NetworkContext;
  using NetworkSetup = fuchsia::netemul::network::NetworkSetup;
  using EndpointSetup = fuchsia::netemul::network::EndpointSetup;
  using FSetupHandle = fuchsia::netemul::network::SetupHandle;

  explicit NetworkContext(async_dispatcher_t* dispatcher = nullptr);
  ~NetworkContext();

  async_dispatcher_t* dispatcher() { return dispatcher_; }

  NetworkManager& network_manager() { return network_manager_; }

  EndpointManager& endpoint_manager() { return endpoint_manager_; }

  zx_status_t Setup(std::vector<NetworkSetup> setup,
                    fidl::InterfaceRequest<FSetupHandle> req);

  void GetNetworkManager(
      ::fidl::InterfaceRequest<NetworkManager::FNetworkManager> net_manager)
      override;
  void GetEndpointManager(
      fidl::InterfaceRequest<EndpointManager::FEndpointManager> endp_manager)
      override;
  void Setup(std::vector<NetworkSetup> setup, SetupCallback callback) override;

  fidl::InterfaceRequestHandler<FNetworkContext> GetHandler();

 private:
  async_dispatcher_t* dispatcher_;
  NetworkManager network_manager_;
  EndpointManager endpoint_manager_;
  std::vector<std::unique_ptr<SetupHandle>> setup_handles_;
  fidl::BindingSet<FNetworkContext> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkContext);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_CONTEXT_H_
