// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_map>
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
                    const std::string& instance_name, Media media, Mdns::Publisher* publisher);

  ~InstanceResponder() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name, const MdnsAddresses& addresses) override;

  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                       const ReplyAddress& sender_address) override;

  void Quit() override;

  // Sets the subtypes to publish.
  void SetSubtypes(std::vector<std::string> subtypes);

  // Reannounces the service instance.
  void Reannounce();

 private:
  static constexpr zx::duration kInitialAnnouncementInterval = zx::sec(1);
  static constexpr zx::duration kMaxAnnouncementInterval = zx::sec(4);
  static constexpr zx::duration kMinMulticastInterval = zx::sec(1);
  static constexpr zx::duration kIdleCheckInterval = zx::sec(60);
  static constexpr zx::time kThrottleStateIdle = zx::time::infinite_past();
  static constexpr zx::time kThrottleStatePending = zx::time::infinite();
  static constexpr size_t kMaxSenderAddresses = 64;

  // Logs a sender address for a future |GetPublication| call.
  void LogSenderAddress(const ReplyAddress& sender_address);

  // Sends an announcement and schedules the next announcement, as appropriate.
  void SendAnnouncement();

  // Sends a reply to a query for any service.
  void SendAnyServiceResponse(const ReplyAddress& reply_address);

  // Calls |GetAndSendPublication| with |query| set to true after first determining if the send
  // should be throttled.
  void MaybeGetAndSendPublication(const std::string& subtype, const ReplyAddress& reply_address);

  // Gets an |Mdns::Publication| from |mdns_responder_| and, if not null, sends
  // it. An empty |subtype| indicates no subtype.
  void GetAndSendPublication(bool query, const std::string& subtype,
                             const ReplyAddress& reply_address);

  // Sends a publication. An empty |subtype| indicates no subtype.
  void SendPublication(const Mdns::Publication& publication, const std::string& subtype,
                       const ReplyAddress& reply_address) const;

  // Sends a subtype PTR record for this instance.
  void SendSubtypePtrRecord(const std::string& subtype, uint32_t ttl,
                            const ReplyAddress& reply_address) const;

  // Sends a publication with zero ttls, indicating the service instance is
  // no longer published.
  void SendGoodbye() const;

  // Frees resources associated with |subtype| if they're no longer required.
  void IdleCheck(const std::string& subtype);

  // Returns the correct multicast reply address depending on |media_|.
  ReplyAddress multicast_reply() const;

  std::string host_full_name_;
  std::string service_name_;
  std::string instance_name_;
  std::string instance_full_name_;
  Media media_;
  Mdns::Publisher* publisher_;
  std::vector<std::string> subtypes_;
  zx::duration announcement_interval_ = kInitialAnnouncementInterval;
  std::unordered_map<std::string, zx::time> throttle_state_by_subtype_;
  std::vector<inet::SocketAddress> sender_addresses_;

 public:
  // Disallow copy, assign and move.
  InstanceResponder(const InstanceResponder&) = delete;
  InstanceResponder(InstanceResponder&&) = delete;
  InstanceResponder& operator=(const InstanceResponder&) = delete;
  InstanceResponder& operator=(InstanceResponder&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_RESPONDER_H_
