// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_RESOLVER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_RESOLVER_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_set>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/ip_address.h"

namespace mdns {

// Requests host name resolution.
class HostNameResolver : public MdnsAgent {
 public:
  // Creates a |HostNameResolver|.
  HostNameResolver(MdnsAgent::Owner* owner, const std::string& host_name, Media media,
                   IpVersions ip_versions, bool include_local, bool include_local_proxies,
                   zx::duration timeout, Mdns::ResolveHostNameCallback callback);

  ~HostNameResolver() override;

  // MdnsAgent overrides.
  void Start(const std::string& local_host_full_name) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                       ReplyAddress sender_address) override;

  void EndOfMessage() override;

  void OnAddProxyHost(const std::string& host_full_name,
                      const std::vector<HostAddress>& addresses) override;

  void Quit() override;

 private:
  std::vector<HostAddress> addresses() const {
    std::vector<HostAddress> result;
    result.assign(addresses_.begin(), addresses_.end());
    return result;
  }

  void InvokeCallbackAndRemoveSelf();

  std::string host_name_;
  std::string host_full_name_;
  Media media_;
  IpVersions ip_versions_;
  bool include_local_;
  bool include_local_proxies_;
  zx::duration timeout_;
  Mdns::ResolveHostNameCallback callback_;
  std::unordered_set<HostAddress, HostAddress::Hash> addresses_;

 public:
  // Disallow copy, assign and move.
  HostNameResolver(const HostNameResolver&) = delete;
  HostNameResolver(HostNameResolver&&) = delete;
  HostNameResolver& operator=(const HostNameResolver&) = delete;
  HostNameResolver& operator=(HostNameResolver&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_RESOLVER_H_
