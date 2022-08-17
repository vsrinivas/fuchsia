// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_REQUESTOR_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_REQUESTOR_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// Searches for instances of a service type.
class InstanceRequestor : public MdnsAgent {
 public:
  // Creates an |InstanceRequestor|.
  InstanceRequestor(MdnsAgent::Owner* owner, const std::string& service_name, Media media,
                    IpVersions ip_versions, bool include_local, bool include_local_proxies);

  ~InstanceRequestor() override = default;

  // Adds the subscriber.
  void AddSubscriber(Mdns::Subscriber* subscriber);

  // Removes the subscriber. If it's the last subscriber, this
  // |InstanceRequestor| is destroyed.
  void RemoveSubscriber(Mdns::Subscriber* subscriber);

  // MdnsAgent overrides.
  void Start(const std::string& local_host_full_name) override;

  void ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                       ReplyAddress sender_address) override;

  void EndOfMessage() override;

  void OnAddLocalServiceInstance(const Mdns::ServiceInstance& instance, bool from_proxy) override;

  void OnChangeLocalServiceInstance(const Mdns::ServiceInstance& instance,
                                    bool from_proxy) override;

  void OnRemoveLocalServiceInstance(const std::string& service_name,
                                    const std::string& instance_name, bool from_proxy) override;

 private:
  // Describes a service instance.
  struct InstanceInfo {
    std::string instance_name_;
    std::string target_;
    std::string target_full_name_;
    inet::IpPort port_;
    std::vector<std::vector<uint8_t>> text_;
    uint16_t srv_priority_ = 0;
    uint16_t srv_weight_ = 0;
    bool new_ = true;
    bool dirty_ = true;
  };

  // Describes a target. We use |inet::SocketAddress| because it has scope_id. The port numbers
  // aren't used. |InstanceInfo| provides the service instance's port number.
  struct TargetInfo {
    std::unordered_set<inet::SocketAddress> addresses_;
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
                        TargetInfo* target_info, uint32_t scope_id);

  void ReceiveAaaaResource(const DnsResource& resource, MdnsResourceSection section,
                           TargetInfo* target_info, uint32_t scope_id);

  void RemoveInstance(const std::string& instance_full_name);

  std::vector<inet::SocketAddress> Addresses(const TargetInfo& target_info, inet::IpPort port);

  std::string service_name_;
  std::string service_full_name_;
  Media media_;
  IpVersions ip_versions_;
  bool include_local_;
  bool include_local_proxies_;
  std::unordered_set<Mdns::Subscriber*> subscribers_;
  std::unordered_map<std::string, InstanceInfo> instance_infos_by_full_name_;
  std::unordered_map<std::string, TargetInfo> target_infos_by_full_name_;
  zx::duration query_delay_;
  std::shared_ptr<DnsQuestion> question_;

 public:
  // Disallow copy, assign and move.
  InstanceRequestor(const InstanceRequestor&) = delete;
  InstanceRequestor(InstanceRequestor&&) = delete;
  InstanceRequestor& operator=(const InstanceRequestor&) = delete;
  InstanceRequestor& operator=(InstanceRequestor&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_REQUESTOR_H_
