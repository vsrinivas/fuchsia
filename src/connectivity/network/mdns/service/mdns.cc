// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <iostream>
#include <limits>
#include <unordered_set>

#include "src/connectivity/network/mdns/service/address_prober.h"
#include "src/connectivity/network/mdns/service/address_responder.h"
#include "src/connectivity/network/mdns/service/dns_formatting.h"
#include "src/connectivity/network/mdns/service/host_name_resolver.h"
#include "src/connectivity/network/mdns/service/instance_prober.h"
#include "src/connectivity/network/mdns/service/instance_requestor.h"
#include "src/connectivity/network/mdns/service/instance_responder.h"
#include "src/connectivity/network/mdns/service/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/mdns_names.h"
#include "src/connectivity/network/mdns/service/resource_renewer.h"

namespace mdns {

Mdns::Mdns(Transceiver& transceiver)
    : dispatcher_(async_get_default_dispatcher()), transceiver_(transceiver) {}

Mdns::~Mdns() {}

void Mdns::SetVerbose(bool verbose) {
#ifdef MDNS_TRACE
  verbose_ = verbose;
#endif  // MDNS_TRACE
}

void Mdns::Start(fuchsia::netstack::NetstackPtr netstack, const std::string& host_name,
                 const MdnsAddresses& addresses, bool perform_address_probe,
                 fit::closure ready_callback) {
  FX_DCHECK(!host_name.empty());
  FX_DCHECK(ready_callback);
  FX_DCHECK(state_ == State::kNotStarted);

  ready_callback_ = std::move(ready_callback);
  state_ = State::kWaitingForInterfaces;

  original_host_name_ = host_name;
  addresses_ = &addresses;

  // Create a resource renewer agent to keep resources alive.
  resource_renewer_ = std::make_shared<ResourceRenewer>(this);

  // Create an address responder agent to respond to address queries.
  AddAgent(std::make_shared<AddressResponder>(this));

  transceiver_.Start(
      std::move(netstack), *addresses_,
      [this, perform_address_probe]() {
        // TODO(dalesat): Link changes that create host name conflicts.
        // Once we have a NIC and we've decided on a unique host name, we
        // don't do any more address probes. This means that we could have link
        // changes that cause two hosts with the same name to be on the same
        // subnet. To improve matters, we need to be prepared to change a host
        // name we've been using for awhile.
        if (state_ == State::kWaitingForInterfaces && transceiver_.HasInterfaces()) {
          OnInterfacesStarted(original_host_name_, perform_address_probe);
        }
      },
      [this](std::unique_ptr<DnsMessage> message, const ReplyAddress& reply_address) {
#ifdef MDNS_TRACE
        if (verbose_) {
          FX_LOGS(INFO) << "Inbound message from " << reply_address << ":" << *message;
        }
#endif  // MDNS_TRACE

        // We'll send messages when we're done processing this inbound message, so don't respond
        // to |FlushSentItems| in the interim. 
        defer_flush_ = true;

        for (auto& question : message->questions_) {
          // We reply to questions using unicast if specifically requested in
          // the question or if the sender's port isn't 5353.
          ReceiveQuestion(*question,
                          (question->unicast_response_ ||
                           reply_address.socket_address().port() != addresses_->port())
                              ? reply_address
                              : addresses_->multicast_reply(),
                          reply_address);
        }

        for (auto& resource : message->answers_) {
          ReceiveResource(*resource, MdnsResourceSection::kAnswer);
        }

        for (auto& resource : message->authorities_) {
          ReceiveResource(*resource, MdnsResourceSection::kAuthority);
        }

        for (auto& resource : message->additionals_) {
          ReceiveResource(*resource, MdnsResourceSection::kAdditional);
        }

        resource_renewer_->EndOfMessage();
        DPROHIBIT_AGENT_REMOVAL();
        for (auto& agent : agents_) {
          agent->EndOfMessage();
        }
        DALLOW_AGENT_REMOVAL();

        defer_flush_ = false;

        SendMessages();
      });

  // The interface monitor may have already found interfaces. In that case,
  // start the address probe in case we don't get any link change notifications.
  if (state_ == State::kWaitingForInterfaces && transceiver_.HasInterfaces()) {
    OnInterfacesStarted(original_host_name_, perform_address_probe);
  }
}

void Mdns::Stop() {
  transceiver_.Stop();
  ready_callback_ = nullptr;
  state_ = State::kNotStarted;
}

void Mdns::ResolveHostName(const std::string& host_name, zx::time timeout,
                           ResolveHostNameCallback callback) {
  FX_DCHECK(MdnsNames::IsValidHostName(host_name));
  FX_DCHECK(callback);
  FX_DCHECK(state_ == State::kActive);

  AddAgent(std::make_shared<HostNameResolver>(this, host_name, timeout, std::move(callback)));
}

void Mdns::SubscribeToService(const std::string& service_name, Subscriber* subscriber) {
  FX_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FX_DCHECK(subscriber);
  FX_DCHECK(state_ == State::kActive);

  std::shared_ptr<InstanceRequestor> agent;

  auto iter = instance_requestors_by_service_name_.find(service_name);
  if (iter == instance_requestors_by_service_name_.end()) {
    agent = std::make_shared<InstanceRequestor>(this, service_name);

    instance_requestors_by_service_name_.emplace(service_name, agent);
    agent->SetOnQuitCallback(
        [this, service_name]() { instance_requestors_by_service_name_.erase(service_name); });

    subscriber->Connect(agent);

    // Add the subscriber before calling AddAgent (which starts the agent), so the subscriber will
    // be notified of the first query.
    agent->AddSubscriber(subscriber);
    AddAgent(agent);
  } else {
    agent = iter->second;
    subscriber->Connect(agent);
    agent->AddSubscriber(subscriber);
  }
}

bool Mdns::PublishServiceInstance(const std::string& service_name, const std::string& instance_name,
                                  bool perform_probe, Publisher* publisher) {
  FX_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FX_DCHECK(MdnsNames::IsValidInstanceName(instance_name));
  FX_DCHECK(publisher);
  FX_DCHECK(state_ == State::kActive);

  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance_name, service_name);

  if (instance_responders_by_instance_full_name_.find(instance_full_name) !=
      instance_responders_by_instance_full_name_.end()) {
    return false;
  }

  auto agent = std::make_shared<InstanceResponder>(this, service_name, instance_name, publisher);

  instance_responders_by_instance_full_name_.emplace(instance_full_name, agent);
  agent->SetOnQuitCallback([this, instance_full_name]() {
    instance_responders_by_instance_full_name_.erase(instance_full_name);
  });

  publisher->Connect(agent);

  if (perform_probe) {
    // We're using a bogus port number here, which is OK, because the 'proposed'
    // resource created from it is only used for collision resolution.
    auto prober = std::make_shared<InstanceProber>(
        this, service_name, instance_name, inet::IpPort::From_uint16_t(0),
        [this, instance_full_name, agent, publisher](bool successful) {
          publisher->DisconnectProber();

          if (!successful) {
            publisher->ReportSuccess(false);
            instance_responders_by_instance_full_name_.erase(instance_full_name);
            return;
          }

          publisher->ReportSuccess(true);
          AddAgent(agent);
        });

    AddAgent(prober);
    publisher->ConnectProber(prober);
  } else {
    publisher->ReportSuccess(true);
    AddAgent(agent);
  }

  return true;
}

void Mdns::LogTraffic() { transceiver_.LogTraffic(); }

void Mdns::OnInterfacesStarted(const std::string& host_name, bool perform_address_probe) {
  if (perform_address_probe) {
    StartAddressProbe(host_name);
    return;
  }

  RegisterHostName(host_name);
  OnReady();
}

void Mdns::StartAddressProbe(const std::string& host_name) {
  state_ = State::kAddressProbeInProgress;

  RegisterHostName(host_name);
  std::cout << "mDNS: Verifying uniqueness of host name " << host_full_name_ << "\n";

  // Create an address prober to look for host name conflicts. The address
  // prober removes itself immediately before it calls the callback.
  auto address_prober = std::make_shared<AddressProber>(this, [this](bool successful) {
    FX_DCHECK(agents_.empty());

    if (!successful) {
      std::cout << "mDNS: Another host is using name " << host_full_name_ << "\n";
      OnHostNameConflict();
      return;
    }

    OnReady();
  });

  // We don't use |AddAgent| here, because agents added that way don't
  // actually participate until we're done probing for host name conflicts.
  agents_.emplace(address_prober);
  address_prober->Start(host_full_name_, *addresses_);
  SendMessages();
}

void Mdns::RegisterHostName(const std::string& host_name) {
  host_name_ = host_name;
  host_full_name_ = MdnsNames::LocalHostFullName(host_name);
  address_placeholder_ = std::make_shared<DnsResource>(host_full_name_, DnsType::kA);
}

void Mdns::OnReady() {
  std::cout << "mDNS: Using unique host name " << host_full_name_ << "\n";

  // Start all the agents.
  state_ = State::kActive;

  // |resource_renewer_| doesn't need to be started, but we do it
  // anyway in case that changes.
  resource_renewer_->Start(host_full_name_, *addresses_);

  for (auto& agent : agents_awaiting_start_) {
    AddAgent(agent);
  }

  agents_awaiting_start_.clear();

  // Let the client know we're ready.
  FX_DCHECK(ready_callback_);
  ready_callback_();
  ready_callback_ = nullptr;
}

void Mdns::OnHostNameConflict() {
  // TODO(dalesat): Support other renaming strategies?
  std::ostringstream os;
  os << original_host_name_ << next_host_name_deduplicator_;
  ++next_host_name_deduplicator_;

  StartAddressProbe(os.str());
}

zx::time Mdns::now() { return zx::clock::get_monotonic(); }

void Mdns::PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) {
  task_queue_.emplace(agent, std::move(task), target_time);
  PostTask();
}

void Mdns::SendQuestion(std::shared_ptr<DnsQuestion> question) {
  FX_DCHECK(question);
  outbound_message_builders_by_reply_address_[addresses_->multicast_reply()].AddQuestion(question);
}

void Mdns::SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                        const ReplyAddress& reply_address) {
  FX_DCHECK(resource);
  // |reply_address| should never be the V6 multicast address, because the V4 multicast address
  // is used to indicate a multicast reply.
  FX_DCHECK(reply_address.socket_address() != addresses_->v6_multicast());

  if (section == MdnsResourceSection::kExpired) {
    // Expirations are distributed to local agents. We handle this case
    // separately so we don't create an empty outbound message.
    prohibit_agent_removal_ = true;

    for (auto& agent : agents_) {
      agent->ReceiveResource(*resource, MdnsResourceSection::kExpired);
    }

    prohibit_agent_removal_ = false;
    return;
  }

  outbound_message_builders_by_reply_address_[reply_address].AddResource(resource, section);
}

void Mdns::SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) {
  SendResource(address_placeholder_, section, reply_address);
}

void Mdns::Renew(const DnsResource& resource) { resource_renewer_->Renew(resource); }

void Mdns::RemoveAgent(std::shared_ptr<MdnsAgent> agent) {
  FX_DCHECK(agent);
  FX_DCHECK(!prohibit_agent_removal_);

  agents_.erase(agent);

  // Remove all pending tasks posted by this agent.
  std::priority_queue<TaskQueueEntry> temp;
  task_queue_.swap(temp);

  while (!temp.empty()) {
    if (temp.top().agent_ != agent.get()) {
      task_queue_.emplace(temp.top().agent_, std::move(temp.top().task_), temp.top().time_);
    }

    temp.pop();
  }

  // In case the agent sent an epitaph.
  SendMessages();
}

void Mdns::FlushSentItems() {
  if (defer_flush_) {
    // |SendMessages| will be called soon, so we don't want to call it now. This allows agents
    // to call |FlushSentItems| synchronous with inbound message processing and posted task
    // execution without unnecessarily fragmenting outgoing messages.
    return;
  }

  SendMessages();
}

void Mdns::AddAgent(std::shared_ptr<MdnsAgent> agent) {
  if (state_ == State::kActive) {
    agents_.emplace(agent);
    FX_DCHECK(!host_full_name_.empty());
    agent->Start(host_full_name_, *addresses_);
    SendMessages();
  } else {
    agents_awaiting_start_.push_back(agent);
  }
}

void Mdns::SendMessages() {
  for (auto& [reply_address, builder] : outbound_message_builders_by_reply_address_) {
    DnsMessage message;
    builder.Build(message);

#ifdef MDNS_TRACE
    if (verbose_) {
      if (reply_address == addresses_->multicast_reply()) {
        FX_LOGS(INFO) << "Outbound message (multicast): " << message;
      } else {
        FX_LOGS(INFO) << "Outbound message to " << reply_address << ":" << message;
      }
    }
#endif  // MDNS_TRACE

    transceiver_.SendMessage(&message, reply_address);
  }

  outbound_message_builders_by_reply_address_.clear();
}

void Mdns::ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                           const ReplyAddress& sender_address) {
  // |reply_address| should never be the V6 multicast address, because the V4 multicast address
  // is used to indicate a multicast reply.
  FX_DCHECK(reply_address.socket_address() != addresses_->v6_multicast());

  // Renewer doesn't need questions.
  DPROHIBIT_AGENT_REMOVAL();
  for (auto& agent : agents_) {
    agent->ReceiveQuestion(question, reply_address, sender_address);
  }

  DALLOW_AGENT_REMOVAL();
}

void Mdns::ReceiveResource(const DnsResource& resource, MdnsResourceSection section) {
  // Renewer is always first.
  resource_renewer_->ReceiveResource(resource, section);
  DPROHIBIT_AGENT_REMOVAL();
  for (auto& agent : agents_) {
    agent->ReceiveResource(resource, section);
  }

  DALLOW_AGENT_REMOVAL();
}

void Mdns::PostTask() {
  FX_DCHECK(!task_queue_.empty());

  if (task_queue_.top().time_ >= posted_task_time_) {
    return;
  }

  posted_task_time_ = task_queue_.top().time_;

  async::PostTaskForTime(
      dispatcher_,
      [this]() {
        // Suppress recursive calls to this method.
        posted_task_time_ = zx::time::infinite_past();

        zx::time now = this->now();

        // We'll send messages when we're done running ready tasks, so don't respond to
        // |FlushSentItems| in the interim. 
        defer_flush_ = true;

        while (!task_queue_.empty() && task_queue_.top().time_ <= now) {
          fit::closure task = std::move(task_queue_.top().task_);
          task_queue_.pop();
          task();
        }

        defer_flush_ = false;

        SendMessages();

        posted_task_time_ = zx::time::infinite();
        if (!task_queue_.empty()) {
          PostTask();
        }
      },
      zx::time(posted_task_time_));
}

///////////////////////////////////////////////////////////////////////////////

// static
std::unique_ptr<Mdns::Publication> Mdns::Publication::Create(inet::IpPort port,
                                                             const std::vector<std::string>& text,
                                                             uint16_t srv_priority,
                                                             uint16_t srv_weight) {
  auto publication = std::make_unique<Publication>();
  publication->port_ = port;
  publication->text_ = text;
  publication->srv_priority_ = srv_priority;
  publication->srv_weight_ = srv_weight;
  return publication;
}

std::unique_ptr<Mdns::Publication> Mdns::Publication::Clone() {
  auto result = Create(port_, text_, srv_priority_, srv_weight_);
  result->ptr_ttl_seconds_ = ptr_ttl_seconds_;
  result->srv_ttl_seconds_ = srv_ttl_seconds_;
  result->txt_ttl_seconds_ = txt_ttl_seconds_;
  return result;
}

///////////////////////////////////////////////////////////////////////////////

Mdns::Subscriber::~Subscriber() { Unsubscribe(); }

void Mdns::Subscriber::Connect(std::shared_ptr<InstanceRequestor> instance_requestor) {
  FX_DCHECK(instance_requestor);
  instance_requestor_ = instance_requestor;
}

void Mdns::Subscriber::Unsubscribe() {
  if (instance_requestor_) {
    instance_requestor_->RemoveSubscriber(this);
    instance_requestor_ = nullptr;
  }
}

///////////////////////////////////////////////////////////////////////////////

Mdns::Publisher::~Publisher() { Unpublish(); }

void Mdns::Publisher::SetSubtypes(std::vector<std::string> subtypes) {
  if (instance_responder_) {
    instance_responder_->SetSubtypes(std::move(subtypes));
  }
}

void Mdns::Publisher::Reannounce() {
  if (instance_responder_) {
    instance_responder_->Reannounce();
  }
}

void Mdns::Publisher::Unpublish() {
  if (instance_prober_) {
    instance_prober_->Quit();
    instance_prober_ = nullptr;
  }

  if (instance_responder_) {
    instance_responder_->Quit();
    instance_responder_ = nullptr;
  }
}

void Mdns::Publisher::Connect(std::shared_ptr<InstanceResponder> instance_responder) {
  FX_DCHECK(instance_responder);
  instance_responder_ = instance_responder;
}

void Mdns::Publisher::ConnectProber(std::shared_ptr<InstanceProber> instance_prober) {
  FX_DCHECK(instance_prober);
  instance_prober_ = instance_prober;
}

void Mdns::Publisher::DisconnectProber() {
  FX_DCHECK(instance_prober_);
  instance_prober_ = nullptr;
}

}  // namespace mdns
