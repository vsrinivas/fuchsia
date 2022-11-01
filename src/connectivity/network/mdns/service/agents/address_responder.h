// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_RESPONDER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_RESPONDER_H_

#include <memory>
#include <string>
#include <vector>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/lib/inet/ip_port.h"

namespace mdns {

// Responds to address resolution requests.
class AddressResponder : public MdnsAgent {
 public:
  // Creates an |AddressResponder| that responds to queries for the local host name with local
  // addresses.
  AddressResponder(MdnsAgent::Owner* owner, Media media, IpVersions ip_versions);

  // Creates an |AddressResponder| that responds to queries for the specified host name with the
  // specified addresses. If no addresses are supplied, the responder will respond to queries with
  // local addresses.
  AddressResponder(MdnsAgent::Owner* owner, std::string host_full_name,
                   std::vector<inet::IpAddress> addresses, Media media, IpVersions ip_versions);

  ~AddressResponder() override;

  std::vector<HostAddress> addresses() const;

  // MdnsAgent overrides.
  void Start(const std::string& local_host_full_name) override;

  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                       const ReplyAddress& sender_address) override;

 private:
  static constexpr zx::duration kMinMulticastInterval = zx::sec(1);
  static constexpr zx::time kThrottleStateIdle = zx::time::infinite_past();
  static constexpr zx::time kThrottleStatePending = zx::time::infinite();

  void MaybeSendAddresses(ReplyAddress reply_address);

  void SendAddressResources(ReplyAddress reply_address);

  std::string host_full_name_;
  std::vector<inet::IpAddress> addresses_;
  Media media_;
  IpVersions ip_versions_;
  zx::time throttle_state_ = kThrottleStateIdle;

 public:
  // Disallow copy, assign and move.
  AddressResponder(const AddressResponder&) = delete;
  AddressResponder(AddressResponder&&) = delete;
  AddressResponder& operator=(const AddressResponder&) = delete;
  AddressResponder& operator=(AddressResponder&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_RESPONDER_H_
