// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/netconnector/ip_port.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

// Dynamically publishes an instance of a service type.
class Responder : public MdnsAgent,
                  public std::enable_shared_from_this<Responder> {
 public:
  // Creates an |Responder|. Subtypes in |announced_subtypes| are announced
  // initially. The |MdnsResponder| referenced by |responder_handle| is
  // consulted to determine how queries are handled.
  Responder(MdnsAgent::Host* host,
            const std::string& host_full_name,
            const std::string& service_name,
            const std::string& instance_name,
            const std::vector<std::string>& announced_subtypes,
            fidl::InterfaceHandle<MdnsResponder> responder_handle);

  // Creates an |Responder|. No subtypes are announced. Queries for
  // |service_name| are responded to using the information in |publication|.
  // Queries for subtypes of |service_name| are ignored.
  Responder(MdnsAgent::Host* host,
            const std::string& host_full_name,
            const std::string& service_name,
            const std::string& instance_name,
            MdnsPublicationPtr publication);

  ~Responder() override;

  // MdnsAgent implementation.
  void Start() override;

  void Wake() override;

  void ReceiveQuestion(const DnsQuestion& question) override;

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section) override;

  void EndOfMessage() override;

  void Quit() override;

 private:
  static constexpr uint32_t kMaxAnnouncementInterval = 4;

  // Gets an |MdnsPublication| from |responder_| and, if not null, sends it.
  // An empty |subtype| indicates no subtype.
  void GetAndSendPublication(bool query, const std::string& subtype) const;

  // Sends a publication. An empty |subtype| indicates no subtype.
  void SendPublication(const std::string& subtype,
                       const MdnsPublication& publication) const;

  MdnsAgent::Host* host_;
  std::string host_full_name_;
  std::string service_name_;
  std::string instance_name_;
  std::string instance_full_name_;
  std::vector<std::string> announced_subtypes_;
  MdnsResponderPtr responder_;
  MdnsPublicationPtr publication_;
  uint32_t announcement_interval_ = 1;
};

}  // namespace mdns
}  // namespace netconnector
