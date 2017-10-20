// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/netconnector/ip_port.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"

namespace netconnector {
namespace mdns {

// Responds to address resolution requests.
class AddressResponder : public MdnsAgent {
 public:
  // Creates an |AddressResponder|.
  AddressResponder(MdnsAgent::Host* host, const std::string& host_full_name);

  ~AddressResponder() override;

  // MdnsAgent overrides.
  void ReceiveQuestion(const DnsQuestion& question) override;

 private:
  std::string host_full_name_;
};

}  // namespace mdns
}  // namespace netconnector
