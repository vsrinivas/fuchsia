// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint_manager.h"

#include "network_context.h"

namespace netemul {

EndpointManager::EndpointManager(NetworkContext* context) : parent_(context) {}

void EndpointManager::ListEndpoints(EndpointManager::ListEndpointsCallback callback) {
  std::vector<std::string> rsp;
  rsp.reserve(endpoints_.size());
  for (const auto& x : endpoints_) {
    rsp.push_back(x.first);
  }
  callback(std::move(rsp));
}

zx_status_t EndpointManager::CreateEndpoint(std::string name, Endpoint::Config config,
                                            bool start_online,
                                            fidl::InterfaceRequest<Endpoint::FEndpoint> req) {
  if (name.empty()) {
    // empty name not allowed
    return ZX_ERR_INVALID_ARGS;
  }
  auto [it, inserted] = endpoints_.try_emplace(name, parent_, std::move(name), std::move(config));
  if (!inserted) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  Endpoint& ep = it->second;
  if (zx_status_t status = ep.Startup(*parent_, start_online); status != ZX_OK) {
    endpoints_.erase(it);
    return status;
  }
  ep.SetClosedCallback([this](const Endpoint& e) {
    // erase endpoint from map
    endpoints_.erase(e.name());
  });
  ep.Bind(std::move(req));
  return ZX_OK;
}

void EndpointManager::CreateEndpoint(std::string name, Endpoint::Config config,
                                     EndpointManager::CreateEndpointCallback callback) {
  fidl::InterfaceHandle<Endpoint::FEndpoint> handle;
  zx_status_t status =
      CreateEndpoint(std::move(name), std::move(config), false, handle.NewRequest());
  if (status != ZX_OK) {
    callback(status, {});
  } else {
    callback(status, std::move(handle));
  }
}

Endpoint* EndpointManager::GetEndpoint(const std::string& name) {
  auto ep_it = endpoints_.find(name);
  if (ep_it == endpoints_.end()) {
    return nullptr;
  }
  return &ep_it->second;
}

void EndpointManager::GetEndpoint(std::string name, EndpointManager::GetEndpointCallback callback) {
  auto ep_it = endpoints_.find(name);
  if (ep_it == endpoints_.end()) {
    // no network with such name
    callback({});
  } else {
    Endpoint& net = ep_it->second;
    fidl::InterfaceHandle<Endpoint::FEndpoint> handle;
    net.Bind(handle.NewRequest());
    callback(std::move(handle));
  }
}

zx_status_t EndpointManager::InstallSink(const std::string& endpoint, data::BusConsumer::Ptr sink,
                                         data::Consumer::Ptr* src) {
  auto ep = endpoints_.find(endpoint);
  if (ep == endpoints_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  return ep->second.InstallSink(std::move(sink), src);
}

zx_status_t EndpointManager::RemoveSink(const std::string& endpoint, data::BusConsumer::Ptr sink,
                                        data::Consumer::Ptr* src) {
  auto ep = endpoints_.find(endpoint);
  if (ep == endpoints_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  return ep->second.RemoveSink(std::move(sink), src);
}

void EndpointManager::Bind(fidl::InterfaceRequest<FEndpointManager> request) {
  bindings_.AddBinding(this, std::move(request), parent_->dispatcher());
}

}  // namespace netemul
