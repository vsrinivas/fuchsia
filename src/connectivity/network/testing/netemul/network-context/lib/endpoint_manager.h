// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_NETWORK_CONTEXT_LIB_ENDPOINT_MANAGER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_NETWORK_CONTEXT_LIB_ENDPOINT_MANAGER_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "src/connectivity/network/testing/netemul/network-context/lib/consumer.h"
#include "src/connectivity/network/testing/netemul/network-context/lib/endpoint.h"

namespace netemul {

class NetworkContext;
class EndpointManager : public fuchsia::netemul::network::EndpointManager {
 public:
  using FEndpointManager = fuchsia::netemul::network::EndpointManager;

  explicit EndpointManager(NetworkContext* context);

  // create endpoint
  zx_status_t CreateEndpoint(std::string name, Endpoint::Config config, bool start_online,
                             fidl::InterfaceRequest<Endpoint::FEndpoint> req);

  // gets endpoint with name
  Endpoint* GetEndpoint(const std::string& name);

  // fidl interface implementations:
  void ListEndpoints(ListEndpointsCallback callback) override;
  void CreateEndpoint(std::string name, Endpoint::Config config,
                      CreateEndpointCallback callback) override;
  void GetEndpoint(::std::string name, GetEndpointCallback callback) override;

  // Request to install a data sink on a named endpoint
  zx_status_t InstallSink(const std::string& endpoint, data::BusConsumer::Ptr sink,
                          data::Consumer::Ptr* src);
  // Request to remove a data sink from a named endpoint
  zx_status_t RemoveSink(const std::string& endpoint, data::BusConsumer::Ptr sink,
                         data::Consumer::Ptr* src);

  // Bind request to FIDL service
  void Bind(fidl::InterfaceRequest<FEndpointManager> request);

 private:
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  fidl::BindingSet<FEndpointManager> bindings_;
  std::unordered_map<std::string, Endpoint> endpoints_;
  // Used to tag ethertap interface names with a unique integer ID to avoid name
  // collisions when device cleanup and creation race.
  size_t endpoint_counter_ = 0;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_NETWORK_CONTEXT_LIB_ENDPOINT_MANAGER_H_
