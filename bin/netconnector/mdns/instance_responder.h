// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/netconnector/ip_port.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

// Dynamically publishes an instance of a service type.
class InstanceResponder : public MdnsAgent {
 public:
  using PublishCallback = std::function<void(MdnsResult result)>;

  // Creates an |InstanceResponder|. Subtypes in |announced_subtypes| are
  // announced initially. The |MdnsResponder| referenced by |responder_handle|
  // is consulted to determine how queries are handled.
  InstanceResponder(MdnsAgent::Host* host,
                    const std::string& service_name,
                    const std::string& instance_name,
                    fidl::InterfaceHandle<MdnsResponder> responder_handle);

  // Creates an |InstanceResponder|. No subtypes are announced. Queries for
  // |service_name| are responded to using the information in |publication|.
  // Queries for subtypes of |service_name| are ignored.
  InstanceResponder(MdnsAgent::Host* host,
                    const std::string& service_name,
                    const std::string& instance_name,
                    MdnsPublicationPtr publication,
                    const PublishCallback& callback);

  ~InstanceResponder() override;

  // MdnsAgent overrides.
  void Start(const std::string& host_full_name) override;

  void ReceiveQuestion(const DnsQuestion& question,
                       const ReplyAddress& reply_address) override;

  void Quit() override;

  // Updates the status.
  void UpdateStatus(MdnsResult result);

  // Sets the subtypes to publish.
  void SetSubtypes(std::vector<std::string> subtypes);

  // Reannounces the service instance.
  void Reannounce();

 private:
  static constexpr fxl::TimeDelta kInitialAnnouncementInterval =
      fxl::TimeDelta::FromSeconds(1);
  static constexpr fxl::TimeDelta kMaxAnnouncementInterval =
      fxl::TimeDelta::FromSeconds(4);

  // Sends an announcement and schedules the next announcement, as appropriate.
  void SendAnnouncement();

  // Gets an |MdnsPublication| from |mdns_responder_| and, if not null, sends
  // it. An empty |subtype| indicates no subtype.
  void GetAndSendPublication(bool query,
                             const std::string& subtype = "",
                             const ReplyAddress& reply_address =
                                 MdnsAddresses::kV4MulticastReply) const;

  // Sends a publication. An empty |subtype| indicates no subtype.
  void SendPublication(const MdnsPublication& publication,
                       const std::string& subtype = "",
                       const ReplyAddress& reply_address =
                           MdnsAddresses::kV4MulticastReply) const;

  // Sends a subtype PTR record for this instance.
  void SendSubtypePtrRecord(const std::string& subtype,
                            uint32_t ttl = DnsResource::kShortTimeToLive,
                            const ReplyAddress& reply_address =
                                MdnsAddresses::kV4MulticastReply) const;

  // Turns |publication| into a goodbye by setting the TTLs to zero and sends
  // it via multicast.
  void SendGoodbye(MdnsPublicationPtr publication) const;

  std::string host_full_name_;
  std::string service_name_;
  std::string instance_name_;
  std::string instance_full_name_;
  std::vector<std::string> subtypes_;
  MdnsResponderPtr mdns_responder_;
  MdnsPublicationPtr publication_;
  PublishCallback callback_;
  fxl::TimeDelta announcement_interval_ = kInitialAnnouncementInterval;
  bool should_quit_ = false;
};

}  // namespace mdns
}  // namespace netconnector
