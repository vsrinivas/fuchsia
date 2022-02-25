// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/service_instance_resolver_service_impl.h"

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

ServiceInstanceResolverServiceImpl::ServiceInstanceResolverServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstanceResolver> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::ServiceInstanceResolver>(mdns, std::move(request),
                                                                   std::move(deleter)) {}

void ServiceInstanceResolverServiceImpl::ResolveServiceInstance(
    std::string service, std::string instance, int64_t timeout,
    ResolveServiceInstanceCallback callback) {
  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "ResolveServiceInstance called with invalid service name " << service
                   << ", closing connection.";
    Quit();
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance)) {
    FX_LOGS(ERROR) << "ResolveServiceInstance called with invalid instance name " << instance
                   << ", closing connection.";
    Quit();
    return;
  }

  mdns().ResolveServiceInstance(
      service, instance, zx::clock::get_monotonic() + zx::nsec(timeout),
      [callback = std::move(callback)](fuchsia::net::mdns::ServiceInstance instance) {
        callback(std::move(instance));
      });
}

}  // namespace mdns
