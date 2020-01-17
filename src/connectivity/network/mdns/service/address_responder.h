// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_RESPONDER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_RESPONDER_H_

#include <memory>
#include <string>
#include <vector>

#include "src/connectivity/network/mdns/service/mdns_agent.h"
#include "src/lib/inet/ip_port.h"

namespace mdns {

// Responds to address resolution requests.
class AddressResponder : public MdnsAgent {
 public:
  // Creates an |AddressResponder|.
  AddressResponder(MdnsAgent::Host* host);

  ~AddressResponder() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name, const MdnsAddresses& addresses) override;

  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address) override;

 private:
  std::string host_full_name_;

 public:
  // Disallow copy, assign and move.
  AddressResponder(const AddressResponder&) = delete;
  AddressResponder(AddressResponder&&) = delete;
  AddressResponder& operator=(const AddressResponder&) = delete;
  AddressResponder& operator=(AddressResponder&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_RESPONDER_H_
