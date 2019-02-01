// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint_manager.h"
#include "network_context.h"

namespace netemul {

EndpointManager::EndpointManager(NetworkContext* context) : parent_(context) {}

void EndpointManager::ListEndpoints(
    EndpointManager::ListEndpointsCallback callback) {
  std::vector<std::string> rsp;
  rsp.reserve(endpoints_.size());
  for (const auto& x : endpoints_) {
    rsp.push_back(x.first);
  }
  callback(std::move(rsp));
}

void EndpointManager::CreateEndpoint(
    ::std::string name, fuchsia::netemul::network::EndpointConfig config,
    EndpointManager::CreateEndpointCallback callback) {
  zx_status_t result = ZX_OK;
  fidl::InterfaceHandle<Endpoint::FEndpoint> handle;
  if (name.empty()) {
    // empty name not allowed
    result = ZX_ERR_INVALID_ARGS;
  } else if (endpoints_.find(name) == endpoints_.end()) {
    // we only support ethertap backing for now
    if (config.backing ==
        fuchsia::netemul::network::EndpointBacking::ETHERTAP) {
      auto ep = std::make_unique<Endpoint>(parent_, std::string(name),
                                           std::move(config));
      result = ep->Startup();

      if (result == ZX_OK) {
        ep->SetClosedCallback([this](const Endpoint& e) {
          // erase endpoint from map
          endpoints_.erase(e.name());
        });

        auto binding = ep->Bind();
        endpoints_.insert(std::make_pair<std::string, Endpoint::Ptr>(
            std::string(name), std::move(ep)));
        handle = std::move(binding);
      }
    } else {
      result = ZX_ERR_INVALID_ARGS;
    }
  } else {
    result = ZX_ERR_ALREADY_EXISTS;
  }

  callback(result, std::move(handle));
}

void EndpointManager::GetEndpoint(
    ::std::string name, EndpointManager::GetEndpointCallback callback) {
  auto ep_it = endpoints_.find(name);
  if (ep_it == endpoints_.end()) {
    // no network with such name
    callback(fidl::InterfaceHandle<Endpoint::FEndpoint>());
  } else {
    auto& net = ep_it->second;
    callback(net->Bind());
  }
}

zx_status_t EndpointManager::InstallSink(std::string endpoint,
                                         data::BusConsumer::Ptr sink,
                                         data::Consumer::Ptr* src) {
  auto ep = endpoints_.find(endpoint);
  if (ep == endpoints_.end()) {
    return ZX_ERR_NOT_FOUND;
  } else {
    return ep->second->InstallSink(std::move(sink), src);
  }
}

zx_status_t EndpointManager::RemoveSink(std::string endpoint,
                                        data::BusConsumer::Ptr sink,
                                        data::Consumer::Ptr* src) {
  auto ep = endpoints_.find(endpoint);
  if (ep == endpoints_.end()) {
    return ZX_ERR_NOT_FOUND;
  } else {
    return ep->second->RemoveSink(std::move(sink), src);
  }
}

void EndpointManager::Bind(fidl::InterfaceRequest<FEndpointManager> request) {
  bindings_.AddBinding(this, std::move(request), parent_->dispatcher());
}

}  // namespace netemul
