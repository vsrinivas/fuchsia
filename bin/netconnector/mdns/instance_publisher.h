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

// Publishes an instance of a service type.
class InstancePublisher
    : public MdnsAgent,
      public std::enable_shared_from_this<InstancePublisher> {
 public:
  // Creates an |InstancePublisher|.
  InstancePublisher(MdnsAgent::Host* host,
                    const std::string& host_full_name,
                    const std::string& instance_full_name,
                    const std::string& service_full_name,
                    IpPort port,
                    const std::vector<std::string>& text);

  ~InstancePublisher() override;

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  void SendRecords(ftl::TimePoint when);

  MdnsAgent::Host* host_;
  std::string instance_full_name_;
  std::string service_full_name_;
  std::shared_ptr<DnsResource> answer_;
  std::vector<std::shared_ptr<DnsResource>> additionals_;
};

}  // namespace mdns
}  // namespace netconnector
