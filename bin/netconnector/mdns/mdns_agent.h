// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/netconnector/mdns/dns_message.h"
#include "garnet/bin/netconnector/mdns/mdns_addresses.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

// kExpired is used when distributing resource expirations. It's not a real
// resource section.
enum class MdnsResourceSection { kAnswer, kAuthority, kAdditional, kExpired };

// Base class for objects that drive mDNS question and record traffic.
class MdnsAgent : public std::enable_shared_from_this<MdnsAgent> {
 public:
  class Host {
   public:
    virtual ~Host() {}

    // Posts a task to be executed at the specified time. Scheduled tasks posted
    // by agents that have since been removed are not executed.
    virtual void PostTaskForTime(MdnsAgent* agent,
                                 fxl::Closure task,
                                 fxl::TimePoint target_time) = 0;

    // Sends a question to the multicast address.
    virtual void SendQuestion(std::shared_ptr<DnsQuestion> question) = 0;

    // Sends a resource to the specified address. The default |reply_address|
    // |kV4MulticastReply| sends the resource to the V4 or V6
    // multicast address.
    virtual void SendResource(std::shared_ptr<DnsResource> resource,
                              MdnsResourceSection section,
                              const ReplyAddress& reply_address) = 0;

    // Sends address resources to the specified address. The default
    // |reply_address| |kV4MulticastReply| sends the addresses to the V4 or V6
    // multicast address.
    virtual void SendAddresses(MdnsResourceSection section,
                               const ReplyAddress& reply_address) = 0;

    // Registers the resource for renewal. See |MdnsAgent::Renew|.
    virtual void Renew(const DnsResource& resource) = 0;

    // Removes the specified agent. |published_instance_full_name| is used for
    // instance publishers only and indicates the full name of a published
    // instance.
    virtual void RemoveAgent(
        const MdnsAgent* agent,
        const std::string& published_instance_full_name) = 0;
  };

  virtual ~MdnsAgent() {}

  // Starts the agent. This method is never called before a shared pointer to
  // the agent is created, so |shared_from_this| is safe to call.
  virtual void Start(const std::string& host_full_name) {}

  // Presents a received question. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void ReceiveQuestion(const DnsQuestion& question,
                               const ReplyAddress& reply_address){};

  // Presents a received resource. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void ReceiveResource(const DnsResource& resource,
                               MdnsResourceSection section){};

  // Signals the end of a message. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void EndOfMessage(){};

  // Tells the agent to quit. The agent should call |RemoveSelf| shortly
  // thereafter. The default calls |RemoveSelf|.
  virtual void Quit() { RemoveSelf(); }

 protected:
  MdnsAgent(Host* host) : host_(host) { FXL_DCHECK(host_); }

  // Posts a task to be executed at the specified time. Scheduled tasks posted
  // by agents that have since been removed are not executed.
  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) {
    host_->PostTaskForTime(this, task, target_time);
  }

  // Sends a question to the multicast address.
  void SendQuestion(std::shared_ptr<DnsQuestion> question) const {
    host_->SendQuestion(question);
  }

  // Sends a resource to the specified address. The default |reply_address|
  // |kV4MulticastReply| sends the resource to the V4 or V6
  // multicast address.
  void SendResource(std::shared_ptr<DnsResource> resource,
                    MdnsResourceSection section,
                    const ReplyAddress& reply_address =
                        MdnsAddresses::kV4MulticastReply) const {
    host_->SendResource(resource, section, reply_address);
  }

  // Sends address resources to the specified address. The default
  // |reply_address| |kV4MulticastReply| sends the addresses to the V4 or V6
  // multicast address.
  void SendAddresses(MdnsResourceSection section,
                     const ReplyAddress& reply_address =
                         MdnsAddresses::kV4MulticastReply) const {
    host_->SendAddresses(section, reply_address);
  }

  // Registers the resource for renewal. Before the resource's TTL expires,
  // an attempt will be made to renew the resource by issuing queries for it.
  // If the renewal is successful, the agent will receive the renewed resource
  // (via |ReceiveResource|) and may choose to renew the resource again.
  // If the renewal fails, the agent will receive a resource record with the
  // same name and type but with a TTL of zero. The section parameter
  // accompanying that resource record will be kExpired.
  //
  // The effect if this call is transient, and there is no way to cancel the
  // renewal. When an agent loses interest in a particular resource, it should
  // simply refrain from renewing the incoming records.
  void Renew(const DnsResource& resource) const { host_->Renew(resource); }

  // Removes this agent. |published_instance_full_name| is used for instance
  // publishers only and indicates the full name of a published instance.
  void RemoveSelf(const std::string& published_instance_full_name = "") const {
    host_->RemoveAgent(this, published_instance_full_name);
  }

 private:
  Host* host_;
};

}  // namespace mdns
}  // namespace netconnector
