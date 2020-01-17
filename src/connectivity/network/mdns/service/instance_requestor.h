// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_REQUESTOR_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_REQUESTOR_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/mdns_agent.h"
#include "src/lib/inet/socket_address.h"

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
  void Start(const std::string& host_full_name, const MdnsAddresses& addresses) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section) override;

  void EndOfMessage() override;

 private:
  struct InstanceInfo {
    std::string instance_name_;
    std::string target_;
    inet::IpPort port_;
    std::vector<std::string> text_;
    uint16_t srv_priority_ = 0;
    uint16_t srv_weight_ = 0;
    bool new_ = true;
    bool dirty_ = true;
  };

  struct TargetInfo {
    inet::IpAddress v4_address_;
    inet::IpAddress v6_address_;
    bool keep_ = false;
    bool dirty_ = false;
  };

  // Report all known instances to the indicated subscriber.
  void ReportAllDiscoveries(Mdns::Subscriber* subscriber);

  // Sends a query for instances and schedules the next query, as appropriate.
  void SendQuery();

  void ReceivePtrResource(const DnsResource& resource, MdnsResourceSection section);

  void ReceiveSrvResource(const DnsResource& resource, MdnsResourceSection section,
                          InstanceInfo* instance_info);

  void ReceiveTxtResource(const DnsResource& resource, MdnsResourceSection section,
                          InstanceInfo* instance_info);

  void ReceiveAResource(const DnsResource& resource, MdnsResourceSection section,
                        TargetInfo* target_info);

  void ReceiveAaaaResource(const DnsResource& resource, MdnsResourceSection section,
                           TargetInfo* target_info);

  void RemoveInstance(const std::string& instance_full_name);

  std::string service_name_;
  std::string service_full_name_;
  std::unordered_set<Mdns::Subscriber*> subscribers_;
  std::unordered_map<std::string, InstanceInfo> instance_infos_by_full_name_;
  std::unordered_map<std::string, TargetInfo> target_infos_by_full_name_;
  zx::duration query_delay_;
  std::shared_ptr<DnsQuestion> question_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_REQUESTOR_H_
