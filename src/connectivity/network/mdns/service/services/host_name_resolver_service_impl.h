// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_RESOLVER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_RESOLVER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"

namespace mdns {

class HostNameResolverServiceImpl : public ServiceImplBase<fuchsia::net::mdns::HostNameResolver> {
 public:
  HostNameResolverServiceImpl(Mdns& mdns,
                              fidl::InterfaceRequest<fuchsia::net::mdns::HostNameResolver> request,
                              fit::closure deleter);

  ~HostNameResolverServiceImpl() override = default;

  // fuchsia::net::mdns::HostNameResolver implementation.
  void ResolveHostName(std::string host_name, int64_t timeout_ns,
                       fuchsia::net::mdns::HostNameResolutionOptions options,
                       ResolveHostNameCallback callback) override;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_RESOLVER_SERVICE_IMPL_H_
