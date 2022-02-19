// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_SERVICE_INSTANCE_RESOLVER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_SERVICE_INSTANCE_RESOLVER_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/ip_address.h"

namespace mdns {

// Requests service instance resolution.
class ServiceInstanceResolver : public MdnsAgent {
 public:
  // Creates a |ServiceInstanceResolver|.
  ServiceInstanceResolver(MdnsAgent::Host* host, const std::string& service,
                          const std::string& instance, zx::time timeout,
                          Mdns::ResolveServiceInstanceCallback callback);

  ~ServiceInstanceResolver() override;

  // MdnsAgent overrides.
  void Start(const std::string& service_instance, const MdnsAddresses& addresses) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  std::string service_;
  std::string instance_name_;
  std::string service_instance_;
  fuchsia::net::mdns::ServiceInstance instance_;
  zx::time timeout_;
  Mdns::ResolveServiceInstanceCallback callback_;
  inet::IpPort port_;

 public:
  // Disallow copy, assign and move.
  ServiceInstanceResolver(const ServiceInstanceResolver&) = delete;
  ServiceInstanceResolver(ServiceInstanceResolver&&) = delete;
  ServiceInstanceResolver& operator=(const ServiceInstanceResolver&) = delete;
  ServiceInstanceResolver& operator=(ServiceInstanceResolver&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_SERVICE_INSTANCE_RESOLVER_H_
