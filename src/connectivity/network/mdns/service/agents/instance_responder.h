// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_RESPONDER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_RESPONDER_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/inet/ip_port.h"

namespace mdns {

// Dynamically publishes an instance of a service type.
class InstanceResponder : public MdnsAgent {
 public:
  // Creates an |InstanceResponder|. The publisher is consulted to determine how queries are
  // handled. If |host_name| is empty the local host name will be used. If |addresses| is empty,
  // the local addresses will be used.
  InstanceResponder(MdnsAgent::Owner* owner, std::string host_name,
                    std::vector<inet::IpAddress> addresses, std::string service_name,
                    std::string instance_name, Media media, IpVersions ip_versions,
                    Mdns::Publisher* publisher);

  ~InstanceResponder() override;

  // Returns a pointer to the current service instance or null if it hasn't yet been established.
  // The returned pointer references a private member of this responder, which may be modified when
  // code in this class has an opportunity to run and will be deleted if this responder is deleted.
  const ServiceInstance* service_instance() const { return instance_ready_ ? &instance_ : nullptr; }

  // Indicates whether the instance is published by a local proxy (true) or the local host (false).
  bool from_proxy() const { return !addresses_.empty(); }

  // MdnsAgent overrides.
  void Start(const std::string& local_host_full_name) override;

  void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                       const ReplyAddress& sender_address) override;

  void Quit() override;

  void OnLocalHostAddressesChanged() override;

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
  void MaybeGetAndSendPublication(PublicationCause publication_cause, const std::string& subtype,
                                  const ReplyAddress& reply_address);

  // Gets an |Mdns::Publication| from |mdns_responder_| and, if not null, sends
  // it. An empty |subtype| indicates no subtype.
  void GetAndSendPublication(PublicationCause publication_cause, const std::string& subtype,
                             const ReplyAddress& reply_address);

  // Sends a publication. An empty |subtype| indicates no subtype.
  void SendPublication(const Mdns::Publication& publication, const std::string& subtype,
                       const ReplyAddress& reply_address);

  // Sends a subtype PTR record for this instance.
  void SendSubtypePtrRecord(const std::string& subtype, uint32_t ttl,
                            const ReplyAddress& reply_address) const;

  // Sends a publication with zero ttls, indicating the service instance is
  // no longer published.
  void SendGoodbye();

  // Frees resources associated with |subtype| if they're no longer required.
  void IdleCheck(const std::string& subtype);

  // Returns the correct multicast reply address depending on |media_| and |ip_verions_|.
  ReplyAddress multicast_reply() const { return ReplyAddress::Multicast(media_, ip_versions_); }

  // Constrain multicast reply addresses to the allowed media and ip versions.
  ReplyAddress Constrain(const ReplyAddress& reply_address) {
    if (reply_address.is_multicast_placeholder()) {
      return multicast_reply();
    }

    return reply_address;
  }

  // Updates |instance_.addresses_| using |local_host_addresses| and |port_| for non-proxy services.
  // For proxy services, the addresses of the service and |port_| are used.
  void UpdateInstanceAddresses();

  std::string host_full_name_;
  const std::vector<inet::IpAddress> addresses_;
  Mdns::ServiceInstance instance_;
  inet::IpPort port_;
  bool instance_ready_ = false;
  std::string instance_full_name_;
  Media media_;
  IpVersions ip_versions_;
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

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_INSTANCE_RESPONDER_H_
