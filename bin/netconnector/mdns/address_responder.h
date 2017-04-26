// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "apps/netconnector/src/ip_port.h"
#include "apps/netconnector/src/mdns/mdns_agent.h"

namespace netconnector {
namespace mdns {

// Responds to address resolution requests.
class AddressResponder : public MdnsAgent,
                         public std::enable_shared_from_this<AddressResponder> {
 public:
  static const std::string kName;

  // Creates an |AddressResponder|.
  AddressResponder(MdnsAgent::Host* host, const std::string& host_full_name);

  ~AddressResponder() override;

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  MdnsAgent::Host* host_;
  std::string host_full_name_;
};

}  // namespace mdns
}  // namespace netconnector
