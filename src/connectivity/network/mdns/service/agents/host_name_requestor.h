// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_REQUESTOR_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_REQUESTOR_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_set>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/ip_address.h"

namespace mdns {

// Requests host name resolution.
class HostNameRequestor : public MdnsAgent {
 public:
  // Creates a |HostNameRequestor|.
  HostNameRequestor(MdnsAgent::Owner* owner, const std::string& host_name, Media media,
                    IpVersions ip_versions, bool include_local, bool include_local_proxies);

  ~HostNameRequestor() override;

  // Adds the subscriber.
  void AddSubscriber(Mdns::HostNameSubscriber* subscriber);

  // Removes the subscriber. If it's the last subscriber, this |HostNameRequestor| is destroyed.
  void RemoveSubscriber(Mdns::HostNameSubscriber* subscriber);

  // MdnsAgent overrides.
  void Start(const std::string& local_host_full_name) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                       ReplyAddress sender_address) override;

  void EndOfMessage() override;

  void OnAddProxyHost(const std::string& host_full_name,
                      const std::vector<HostAddress>& addresses) override;

  void OnRemoveProxyHost(const std::string& host_full_name) override;

  void OnLocalHostAddressesChanged() override;

 private:
  std::vector<HostAddress> addresses() const {
    std::vector<HostAddress> result;
    result.assign(addresses_.begin(), addresses_.end());
    return result;
  }

  std::vector<HostAddress> prev_addresses() const {
    std::vector<HostAddress> result;
    result.assign(prev_addresses_.begin(), prev_addresses_.end());
    return result;
  }

  void SendAddressesChanged();

  std::string host_name_;
  std::string host_full_name_;
  Media media_;
  IpVersions ip_versions_;
  bool include_local_;
  bool include_local_proxies_;
  std::string local_host_full_name_;
  std::unordered_set<HostAddress, HostAddress::Hash> addresses_;
  std::unordered_set<HostAddress, HostAddress::Hash> prev_addresses_;
  std::unordered_set<Mdns::HostNameSubscriber*> subscribers_;

 public:
  // Disallow copy, assign and move.
  HostNameRequestor(const HostNameRequestor&) = delete;
  HostNameRequestor(HostNameRequestor&&) = delete;
  HostNameRequestor& operator=(const HostNameRequestor&) = delete;
  HostNameRequestor& operator=(HostNameRequestor&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_HOST_NAME_REQUESTOR_H_
