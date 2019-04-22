// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_ADDRESS_RESPONDER_H_
#define GARNET_BIN_MDNS_SERVICE_ADDRESS_RESPONDER_H_

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/mdns/service/mdns_agent.h"
#include "garnet/lib/inet/ip_port.h"

namespace mdns {

// Responds to address resolution requests.
class AddressResponder : public MdnsAgent {
 public:
  // Creates an |AddressResponder|.
  AddressResponder(MdnsAgent::Host* host);

  ~AddressResponder() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name,
             inet::IpPort mdns_port) override;

  void ReceiveQuestion(const DnsQuestion& question,
                       const ReplyAddress& reply_address) override;

 private:
  std::string host_full_name_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_ADDRESS_RESPONDER_H_
