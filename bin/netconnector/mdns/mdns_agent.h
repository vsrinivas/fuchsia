// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/netconnector/src/mdns/dns_message.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

enum class MdnsResourceSection { kAnswer, kAuthority, kAdditional };

// Abstract class for objects that drive mDNS question and record traffic.
class MdnsAgent {
 public:
  class Host {
   public:
    virtual ~Host() {}

    // Schedules a call to |agent->Wake| at the specified time.
    virtual void WakeAt(std::shared_ptr<MdnsAgent> agent,
                        ftl::TimePoint when) = 0;

    // Sends a question to the multicast address at the specified time.
    virtual void SendQuestion(std::shared_ptr<DnsQuestion> question,
                              ftl::TimePoint when) = 0;

    // Sends a resource to the multicast address at the specified time. After
    // a resource is sent with a TTL of zero, the resource is marked so that
    // it won't get resent. This is useful if the agent has queued up resources
    // to send in the future and later decides to cancel them by setting their
    // TTLs to zero and resending.
    virtual void SendResource(std::shared_ptr<DnsResource> resource,
                              MdnsResourceSection section,
                              ftl::TimePoint when) = 0;

    // Sends address resources to the multicast address at the specified time.
    virtual void SendAddresses(MdnsResourceSection section,
                               ftl::TimePoint when) = 0;

    // Removes the agent with the specified name.
    virtual void RemoveAgent(const std::string& name) = 0;
  };

  virtual ~MdnsAgent() {}

  // Starts the agent. This method is never called before a shared pointer to
  // the agent is created, so |shared_from_this| is safe to call.
  virtual void Start() = 0;

  // Wakes the agent as requested via |Host::WakeAt|.
  virtual void Wake() = 0;

  // Presents a received question.
  virtual void ReceiveQuestion(const DnsQuestion& question) = 0;

  // Presents a received resource.
  virtual void ReceiveResource(const DnsResource& resource,
                               MdnsResourceSection section) = 0;

  // Signals the end of a message.
  virtual void EndOfMessage() = 0;

  // Tells the agent to quit. The agent should call |Host::RemoveAgent| shortly
  // thereafter.
  virtual void Quit() = 0;
};

}  // namespace mdns
}  // namespace netconnector
