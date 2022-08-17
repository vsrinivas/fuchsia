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
  ServiceInstanceResolver(MdnsAgent::Owner* owner, const std::string& service,
                          const std::string& instance, zx::time timeout, Media media,
                          IpVersions ip_versions, bool include_local, bool include_local_proxies,
                          Mdns::ResolveServiceInstanceCallback callback);

  ~ServiceInstanceResolver() override;

  // MdnsAgent overrides.
  void Start(const std::string& service_instance) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                       ReplyAddress sender_address) override;

  void EndOfMessage() override;

  void OnAddLocalServiceInstance(const Mdns::ServiceInstance& instance, bool from_proxy) override;

  void Quit() override;

 private:
  std::string service_;
  std::string instance_name_;
  std::string service_instance_;
  fuchsia::net::mdns::ServiceInstance instance_;
  std::string target_full_name_;
  zx::time timeout_;
  Media media_;
  IpVersions ip_versions_;
  bool include_local_;
  bool include_local_proxies_;
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
