// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_context.h"
#include <lib/async/default.h>

namespace netemul {

NetworkContext::NetworkContext(async_dispatcher_t* dispatcher)
    : network_manager_(this), endpoint_manager_(this) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  dispatcher_ = dispatcher;
}
void NetworkContext::GetNetworkManager(
    ::fidl::InterfaceRequest<NetworkManager::FNetworkManager> net_manager) {
  network_manager_.Bind(std::move(net_manager));
}

void NetworkContext::GetEndpointManager(
    fidl::InterfaceRequest<EndpointManager::FEndpointManager> endp_manager) {
  endpoint_manager_.Bind(std::move(endp_manager));
}

fidl::InterfaceRequestHandler<fuchsia::netemul::network::NetworkContext>
NetworkContext::GetHandler() {
  return [this](fidl::InterfaceRequest<FNetworkContext> request) {
    bindings_.AddBinding(this, std::move(request), dispatcher_);
  };
}

}  // namespace netemul
