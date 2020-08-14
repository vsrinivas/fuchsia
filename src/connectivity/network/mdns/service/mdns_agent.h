// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_AGENT_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_AGENT_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/connectivity/network/mdns/service/dns_message.h"
#include "src/connectivity/network/mdns/service/mdns_addresses.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// kExpired is used when distributing resource expirations. It's not a real
// resource section.
enum class MdnsResourceSection { kAnswer, kAuthority, kAdditional, kExpired };

// Base class for objects that drive mDNS question and record traffic.
//
// Agents that have been 'started' receive all inbound questions and resource records via their
// |ReceiveQuestion| and |ReceiveResource| methods. When the agent host receives an inbound
// message, it calls those methods for each question and resource in the message. When that's
// done, the host calls |EndOfMessage| on each agent.
//
// Agents may call any of the protected 'Send' methods (|SendQuestion|, |SendResource| and
// |SendAddresses|) at any time. The host accumulates the questions and resources and sends
// them in messages. Typically, agents don't have to worry about sending messages. Messages
// are sent for accumulated questions and resources:
//
// 1) after |Start| is called on any agent,
// 2) after an inbound message is processed and all agents have gotten their |EndOfMessage| calls,
// 3) after an agent is removed (in case it wants to say goodbye),
// 4) after the completion of any task posted using |PostTaskForTime|.
//
// If an agent wants a message sent asynchronously with respect to agent start, inbound message
// arrival, agent removal and posted tasks, the agent should call |FlushSentItems|. Calling
// |FlushSentItems| synchronously with those operations isn't harmful.
//
class MdnsAgent : public std::enable_shared_from_this<MdnsAgent> {
 public:
  class Host {
   public:
    virtual ~Host() {}

    // Gets the current time.
    virtual zx::time now() = 0;

    // Posts a task to be executed at the specified time. Scheduled tasks posted
    // by agents that have since been removed are not executed.
    virtual void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) = 0;

    // Sends a question to the multicast address.
    virtual void SendQuestion(std::shared_ptr<DnsQuestion> question) = 0;

    // Sends a resource to the specified address. The default |reply_address|
    // |kV4MulticastReply| sends the resource to the V4 or V6
    // multicast address.
    virtual void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                              const ReplyAddress& reply_address) = 0;

    // Sends address resources to the specified address. The default
    // |reply_address| |kV4MulticastReply| sends the addresses to the V4 or V6
    // multicast address.
    virtual void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) = 0;

    // Registers the resource for renewal. See |MdnsAgent::Renew|.
    virtual void Renew(const DnsResource& resource) = 0;

    // Removes the specified agent.
    virtual void RemoveAgent(std::shared_ptr<MdnsAgent> agent) = 0;

    // Flushes sent questions and resources by sending the appropriate messages.
    virtual void FlushSentItems() = 0;
  };

  virtual ~MdnsAgent() {}

  // Starts the agent. This method is never called before a shared pointer to
  // the agent is created, so |shared_from_this| is safe to call.
  // Specializations should call this method first.
  virtual void Start(const std::string& host_full_name, const MdnsAddresses& addresses) {
    addresses_ = &addresses;
  }

  // Presents a received question. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                               const ReplyAddress& sender_address){};

  // Presents a received resource. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void ReceiveResource(const DnsResource& resource, MdnsResourceSection section){};

  // Signals the end of a message. This agent must not call |RemoveSelf| during
  // a call to this method.
  virtual void EndOfMessage(){};

  // Tells the agent to quit. Any overrides should call this base implementation.
  virtual void Quit() {
    RemoveSelf();
    if (on_quit_) {
      on_quit_();
      on_quit_ = nullptr;
    }
  }

  // Sets the 'on quit' callback that's called when the agent quits. May be called once at most
  // for a given agent.
  void SetOnQuitCallback(fit::closure on_quit) {
    FX_DCHECK(on_quit);
    FX_DCHECK(!on_quit_);
    on_quit_ = std::move(on_quit);
  }

 protected:
  MdnsAgent(Host* host) : host_(host) { FX_DCHECK(host_); }

  bool started() const { return addresses_ != nullptr; }

  const MdnsAddresses& addresses() const {
    FX_DCHECK(addresses_);
    return *addresses_;
  }

  // Gets the current time.
  zx::time now() { return host_->now(); }

  // Posts a task to be executed at the specified time. Scheduled tasks posted
  // by agents that have since been removed are not executed.
  void PostTaskForTime(fit::closure task, zx::time target_time) {
    host_->PostTaskForTime(this, std::move(task), target_time);
  }

  // Sends a question to the multicast address.
  void SendQuestion(std::shared_ptr<DnsQuestion> question) const { host_->SendQuestion(question); }

  // Sends a resource to the specified address.
  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) const {
    host_->SendResource(resource, section, reply_address);
  }

  // Sends address resources to the specified address.
  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) const {
    host_->SendAddresses(section, reply_address);
  }

  // Flushes sent questions and resources by sending the appropriate messages. This method is only
  // needed when questions or resources need to be sent asynchronously with respect to |Start|,
  // |ReceiveQuestion|, |ReceiveResource|, |EndOfMessage|, |Quit| or a task posted using
  // |PostTaskForTime|. See the discussion at the top of the file.
  void FlushSentItems() { host_->FlushSentItems(); }

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

  // Removes this agent.
  void RemoveSelf() { host_->RemoveAgent(shared_from_this()); }

 private:
  Host* host_;
  const MdnsAddresses* addresses_ = nullptr;
  fit::closure on_quit_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_AGENT_H_
