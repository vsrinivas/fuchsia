// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/mdns_agent.h"
#include "src/lib/inet/ip_port.h"

namespace mdns {

// Dynamically publishes an instance of a service type.
class InstanceResponder : public MdnsAgent {
 public:
  // Creates an |InstanceResponder|. The publisher is consulted to determine
  // how queries are handled.
  InstanceResponder(MdnsAgent::Host* host, const std::string& service_name,
                    const std::string& instance_name, Mdns::Publisher* publisher);

  ~InstanceResponder() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name, const MdnsAddresses& addresses) override;

  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address) override;

  void Quit() override;

  // Reports whether the publication attempt was successful. Publication can
  // fail if the service instance is currently be published by another device
  // on the subnet.
  void ReportSuccess(bool success);

  // Sets the subtypes to publish.
  void SetSubtypes(std::vector<std::string> subtypes);

  // Reannounces the service instance.
  void Reannounce();

 private:
  static constexpr zx::duration kInitialAnnouncementInterval = zx::sec(1);
  static constexpr zx::duration kMaxAnnouncementInterval = zx::sec(4);

  // Sends an announcement and schedules the next announcement, as appropriate.
  void SendAnnouncement();

  // Sends a reply to a query for any service.
  void SendAnyServiceResponse(const ReplyAddress& reply_address);

  // Gets an |Mdns::Publication| from |mdns_responder_| and, if not null, sends
  // it. An empty |subtype| indicates no subtype.
  void GetAndSendPublication(bool query, const std::string& subtype,
                             const ReplyAddress& reply_address) const;

  // Sends a publication. An empty |subtype| indicates no subtype.
  void SendPublication(const Mdns::Publication& publication, const std::string& subtype,
                       const ReplyAddress& reply_address) const;

  // Sends a subtype PTR record for this instance.
  void SendSubtypePtrRecord(const std::string& subtype, uint32_t ttl,
                            const ReplyAddress& reply_address) const;

  // Sends a publication with zero ttls, indicating the service instance is
  // no longer published.
  void SendGoodbye() const;

  std::string host_full_name_;
  std::string service_name_;
  std::string instance_name_;
  std::string instance_full_name_;
  Mdns::Publisher* publisher_;
  std::vector<std::string> subtypes_;
  zx::duration announcement_interval_ = kInitialAnnouncementInterval;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_
