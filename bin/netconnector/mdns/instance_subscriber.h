// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/netconnector/src/mdns/mdns_agent.h"
#include "apps/netconnector/src/socket_address.h"
#include "lib/ftl/time/time_delta.h"

namespace netconnector {
namespace mdns {

// Searches for instances of a service type.
class InstanceSubscriber
    : public MdnsAgent,
      public std::enable_shared_from_this<InstanceSubscriber> {
 public:
  using ServiceInstanceCallback =
      std::function<void(const std::string& service,
                         const std::string& instance,
                         const SocketAddress& v4_address,
                         const SocketAddress& v6_address,
                         const std::vector<std::string>& text)>;

  // Creates an |InstanceSubscriber|.
  InstanceSubscriber(MdnsAgent::Host* host,
                     const std::string& service_name,
                     const std::string& service_full_name,
                     const ServiceInstanceCallback& callback);

  ~InstanceSubscriber() override;

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  struct InstanceInfo {
    std::string instance_name_;
    std::string target_;
    IpPort port_;
    std::vector<std::string> text_;
    bool dirty_ = true;
  };

  struct TargetInfo {
    IpAddress v4_address_;
    IpAddress v6_address_;
    bool keep_ = false;
    bool dirty_ = false;
  };

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

  MdnsAgent::Host* host_;
  std::string service_name_;
  std::string service_full_name_;
  ServiceInstanceCallback callback_;
  std::unordered_map<std::string, InstanceInfo> instance_infos_by_full_name_;
  std::unordered_map<std::string, TargetInfo> target_infos_by_full_name_;
  ftl::TimeDelta query_delay_;
  std::shared_ptr<DnsQuestion> question_;
};

}  // namespace mdns
}  // namespace netconnector
