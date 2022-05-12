// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_RESOLVER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_RESOLVER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"

namespace mdns {

class ServiceInstanceResolverServiceImpl
    : public ServiceImplBase<fuchsia::net::mdns::ServiceInstanceResolver> {
 public:
  ServiceInstanceResolverServiceImpl(
      Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstanceResolver> request,
      fit::closure deleter);

  ~ServiceInstanceResolverServiceImpl() override = default;

  // fuchsia::net:mdns::ServiceInstanceResolver implementation.
  void ResolveServiceInstance(std::string service, std::string instance, int64_t timeout,
                              ResolveServiceInstanceCallback callback) override;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_RESOLVER_SERVICE_IMPL_H_
