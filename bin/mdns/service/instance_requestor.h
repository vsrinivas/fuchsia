// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "garnet/bin/mdns/service/mdns.h"
#include "garnet/bin/mdns/service/mdns_agent.h"
#include "garnet/bin/mdns/service/socket_address.h"
#include "lib/fxl/time/time_delta.h"

namespace mdns {

// Searches for instances of a service type.
class InstanceRequestor : public MdnsAgent {
 public:
  // Creates an |InstanceRequestor|.
  InstanceRequestor(MdnsAgent::Host* host, const std::string& service_name);

  ~InstanceRequestor() override;

  // Adds the subscriber.
  void AddSubscriber(Mdns::Subscriber* subscriber);

  // Removes the subscriber. If it's the last subscriber, this
  // |InstanceRequestor| is destroyed.
  void RemoveSubscriber(Mdns::Subscriber* subscriber);

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

 private:
  struct InstanceInfo {
    std::string instance_name_;
    std::string target_;
    IpPort port_;
    std::vector<std::string> text_;
    bool new_ = true;
    bool dirty_ = true;
  };

  struct TargetInfo {
    IpAddress v4_address_;
    IpAddress v6_address_;
    bool keep_ = false;
    bool dirty_ = false;
  };

  // Report all known instances to the indicated subscriber.
  void ReportAllDiscoveries(Mdns::Subscriber* subscriber);

  // Sends a query for instances and schedules the next query, as appropriate.
  void SendQuery();

  void ReceivePtrResource(const DnsResource& resource,
                          MdnsResourceSection section);

  void ReceiveSrvResource(const DnsResource& resource,
                          MdnsResourceSection section,
                          InstanceInfo* instance_info);

  void ReceiveTxtResource(const DnsResource& resource,
                          MdnsResourceSection section,
                          InstanceInfo* instance_info);

  void ReceiveAResource(const DnsResource& resource,
                        MdnsResourceSection section,
                        TargetInfo* target_info);

  void ReceiveAaaaResource(const DnsResource& resource,
                           MdnsResourceSection section,
                           TargetInfo* target_info);

  void RemoveInstance(const std::string& instance_full_name);

  std::string service_name_;
  std::string service_full_name_;
  std::unordered_set<Mdns::Subscriber*> subscribers_;
  std::unordered_map<std::string, InstanceInfo> instance_infos_by_full_name_;
  std::unordered_map<std::string, TargetInfo> target_infos_by_full_name_;
  fxl::TimeDelta query_delay_;
  std::shared_ptr<DnsQuestion> question_;
};

}  // namespace mdns
