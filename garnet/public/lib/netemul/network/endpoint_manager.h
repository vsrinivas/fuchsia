// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_ENDPOINT_MANAGER_H_
#define LIB_NETEMUL_NETWORK_ENDPOINT_MANAGER_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include "lib/netemul/network/consumer.h"
#include "lib/netemul/network/endpoint.h"

#include <unordered_map>

namespace netemul {

class NetworkContext;
class EndpointManager : public fuchsia::netemul::network::EndpointManager {
 public:
  using FEndpointManager = fuchsia::netemul::network::EndpointManager;

  explicit EndpointManager(NetworkContext* context);

  // fidl interface implementations:
  void ListEndpoints(ListEndpointsCallback callback) override;
  void CreateEndpoint(::std::string name,
                      fuchsia::netemul::network::EndpointConfig config,
                      CreateEndpointCallback callback) override;
  void GetEndpoint(::std::string name,
                   GetEndpointCallback callback) override;

  // Request to install a data sink on a named endpoint
  zx_status_t InstallSink(std::string endpoint, data::BusConsumer::Ptr sink,
                          data::Consumer::Ptr* src);
  // Request to remove a data sink from a named endpoint
  zx_status_t RemoveSink(std::string endpoint, data::BusConsumer::Ptr sink,
                         data::Consumer::Ptr* src);

  // Bind request to FIDL service
  void Bind(fidl::InterfaceRequest<FEndpointManager> request);

 private:
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  fidl::BindingSet<FEndpointManager> bindings_;
  std::unordered_map<std::string, Endpoint::Ptr> endpoints_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_ENDPOINT_MANAGER_H_
