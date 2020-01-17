// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
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
#include "src/lib/fxl/logging.h"

namespace mdns {

Mdns::Mdns() : dispatcher_(async_get_default_dispatcher()) {}

Mdns::~Mdns() {}

void Mdns::SetVerbose(bool verbose) {
#ifdef MDNS_TRACE
  verbose_ = verbose;
#endif  // MDNS_TRACE
}

void Mdns::Start(fuchsia::netstack::NetstackPtr netstack, const std::string& host_name,
                 const MdnsAddresses& addresses, bool perform_address_probe,
                 fit::closure ready_callback) {
  FXL_DCHECK(!host_name.empty());
  FXL_DCHECK(ready_callback);
  FXL_DCHECK(state_ == State::kNotStarted);

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
        if (state_ == State::kWaitingForInterfaces && transceiver_.has_interfaces()) {
          OnInterfacesStarted(original_host_name_, perform_address_probe);
        }
      },
      [this](std::unique_ptr<DnsMessage> message, const ReplyAddress& reply_address) {
#ifdef MDNS_TRACE
        if (verbose_) {
          FXL_LOG(INFO) << "Inbound message from " << reply_address << ":" << *message;
        }
#endif  // MDNS_TRACE

        for (auto& question : message->questions_) {
          // We reply to questions using unicast if specifically requested in
          // the question or if the sender's port isn't 5353.
          ReceiveQuestion(*question, (question->unicast_response_ ||
                                      reply_address.socket_address().port() != addresses_->port())
                                         ? reply_address
                                         : addresses_->multicast_reply());
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
        for (auto& pair : agents_) {
          pair.second->EndOfMessage();
        }
        DALLOW_AGENT_REMOVAL();

        SendMessages();
      });

  // The interface monitor may have already found interfaces. In that case,
  // start the address probe in case we don't get any link change notifications.
  if (state_ == State::kWaitingForInterfaces && transceiver_.has_interfaces()) {
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
  FXL_DCHECK(MdnsNames::IsValidHostName(host_name));
  FXL_DCHECK(callback);
  FXL_DCHECK(state_ == State::kActive);

  AddAgent(std::make_shared<HostNameResolver>(this, host_name, timeout, std::move(callback)));
}

void Mdns::SubscribeToService(const std::string& service_name, Subscriber* subscriber) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(subscriber);
  FXL_DCHECK(state_ == State::kActive);

  std::shared_ptr<InstanceRequestor> agent;

  auto iter = instance_subscribers_by_service_name_.find(service_name);
  if (iter == instance_subscribers_by_service_name_.end()) {
    agent = std::make_shared<InstanceRequestor>(this, service_name);
    instance_subscribers_by_service_name_.emplace(service_name, agent);
    AddAgent(agent);
  } else {
    agent = iter->second;
  }

  subscriber->Connect(agent);
  agent->AddSubscriber(subscriber);
}

bool Mdns::PublishServiceInstance(const std::string& service_name, const std::string& instance_name,
                                  bool perform_probe, Publisher* publisher) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));
  FXL_DCHECK(publisher);
  FXL_DCHECK(state_ == State::kActive);

  auto agent = std::make_shared<InstanceResponder>(this, service_name, instance_name, publisher);

  publisher->Connect(agent);

  // We're using a bogus port number here, which is OK, because the 'proposed'
  // resource created from it is only used for collision resolution.
  return AddInstanceResponder(service_name, instance_name, inet::IpPort::From_uint16_t(0), agent,
                              perform_probe);
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
  std::cerr << "mDNS: Verifying uniqueness of host name " << host_full_name_ << "\n";

  // Create an address prober to look for host name conflicts. The address
  // prober removes itself immediately before it calls the callback.
  auto address_prober = std::make_shared<AddressProber>(this, [this](bool successful) {
    FXL_DCHECK(agents_.empty());

    if (!successful) {
      std::cerr << "mDNS: Another host is using name " << host_full_name_ << "\n";
      OnHostNameConflict();
      return;
    }

    OnReady();
  });

  // We don't use |AddAgent| here, because agents added that way don't
  // actually participate until we're done probing for host name conflicts.
  agents_.emplace(address_prober.get(), address_prober);
  address_prober->Start(host_full_name_, *addresses_);
  SendMessages();
}

void Mdns::RegisterHostName(const std::string& host_name) {
  host_name_ = host_name;
  host_full_name_ = MdnsNames::LocalHostFullName(host_name);
  address_placeholder_ = std::make_shared<DnsResource>(host_full_name_, DnsType::kA);
}

void Mdns::OnReady() {
  std::cerr << "mDNS: Using unique host name " << host_full_name_ << "\n";

  // Start all the agents.
  state_ = State::kActive;

  // |resource_renewer_| doesn't need to be started, but we do it
  // anyway in case that changes.
  resource_renewer_->Start(host_full_name_, *addresses_);

  for (auto agent : agents_awaiting_start_) {
    AddAgent(agent);
  }

  agents_awaiting_start_.clear();

  // Let the client know we're ready.
  FXL_DCHECK(ready_callback_);
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
  FXL_DCHECK(question);
  DnsMessage& message = outbound_messages_by_reply_address_[addresses_->multicast_reply()];
  message.questions_.push_back(question);
}

void Mdns::SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                        const ReplyAddress& reply_address) {
  FXL_DCHECK(resource);

  if (section == MdnsResourceSection::kExpired) {
    // Expirations are distributed to local agents. We handle this case
    // separately so we don't create an empty outbound message.
    prohibit_agent_removal_ = true;

    for (auto& pair : agents_) {
      pair.second->ReceiveResource(*resource, MdnsResourceSection::kExpired);
    }

    prohibit_agent_removal_ = false;
    return;
  }

  DnsMessage& message = outbound_messages_by_reply_address_[reply_address];

  switch (section) {
    case MdnsResourceSection::kAnswer:
      message.answers_.push_back(resource);
      break;
    case MdnsResourceSection::kAuthority:
      message.authorities_.push_back(resource);
      break;
    case MdnsResourceSection::kAdditional:
      message.additionals_.push_back(resource);
      break;
    case MdnsResourceSection::kExpired:
      FXL_DCHECK(false);
      break;
  }
}

void Mdns::SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) {
  SendResource(address_placeholder_, section, reply_address);
}

void Mdns::Renew(const DnsResource& resource) { resource_renewer_->Renew(resource); }

void Mdns::RemoveAgent(const MdnsAgent* agent, const std::string& published_instance_full_name) {
  FXL_DCHECK(agent);
  FXL_DCHECK(!prohibit_agent_removal_);

  agents_.erase(agent);

  // Remove all pending tasks posted by this agent.
  std::priority_queue<TaskQueueEntry> temp;
  task_queue_.swap(temp);

  while (!temp.empty()) {
    if (temp.top().agent_ != agent) {
      task_queue_.emplace(temp.top().agent_, std::move(temp.top().task_), temp.top().time_);
    }

    temp.pop();
  }

  if (!published_instance_full_name.empty()) {
    instance_publishers_by_instance_full_name_.erase(published_instance_full_name);
  }

  // In case the agent sent an epitaph.
  SendMessages();
}

void Mdns::AddAgent(std::shared_ptr<MdnsAgent> agent) {
  if (state_ == State::kActive) {
    agents_.emplace(agent.get(), agent);
    FXL_DCHECK(!host_full_name_.empty());
    agent->Start(host_full_name_, *addresses_);
    SendMessages();
  } else {
    agents_awaiting_start_.push_back(agent);
  }
}

bool Mdns::AddInstanceResponder(const std::string& service_name, const std::string& instance_name,
                                inet::IpPort port, std::shared_ptr<InstanceResponder> agent,
                                bool perform_probe) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));

  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance_name, service_name);

  if (instance_publishers_by_instance_full_name_.find(instance_full_name) !=
      instance_publishers_by_instance_full_name_.end()) {
    return false;
  }

  instance_publishers_by_instance_full_name_.emplace(instance_full_name, agent);

  if (perform_probe) {
    auto prober =
        std::make_shared<InstanceProber>(this, service_name, instance_name, port,
                                         [this, instance_full_name, agent](bool successful) {
                                           if (!successful) {
                                             agent->ReportSuccess(false);
                                             return;
                                           }

                                           agent->ReportSuccess(true);
                                           AddAgent(agent);
                                         });

    AddAgent(prober);
  } else {
    agent->ReportSuccess(true);
    AddAgent(agent);
  }

  return true;
}

void Mdns::SendMessages() {
  for (auto& pair : outbound_messages_by_reply_address_) {
    const ReplyAddress& reply_address = pair.first;
    DnsMessage& message = pair.second;

    message.UpdateCounts();

    if (message.questions_.empty()) {
      message.header_.SetResponse(true);
      message.header_.SetAuthoritativeAnswer(true);
    }

#ifdef MDNS_TRACE
    if (verbose_) {
      if (reply_address == addresses_->multicast_reply()) {
        FXL_LOG(INFO) << "Outbound message (multicast): " << message;
      } else {
        FXL_LOG(INFO) << "Outbound message to " << reply_address << ":" << message;
      }
    }
#endif  // MDNS_TRACE

    transceiver_.SendMessage(&message, reply_address);
  }

  outbound_messages_by_reply_address_.clear();
}

void Mdns::ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address) {
  // Renewer doesn't need questions.
  DPROHIBIT_AGENT_REMOVAL();
  for (auto& pair : agents_) {
    pair.second->ReceiveQuestion(question, reply_address);
  }

  DALLOW_AGENT_REMOVAL();
}

void Mdns::ReceiveResource(const DnsResource& resource, MdnsResourceSection section) {
  // Renewer is always first.
  resource_renewer_->ReceiveResource(resource, section);
  DPROHIBIT_AGENT_REMOVAL();
  for (auto& pair : agents_) {
    pair.second->ReceiveResource(resource, section);
  }

  DALLOW_AGENT_REMOVAL();
}

void Mdns::PostTask() {
  FXL_DCHECK(!task_queue_.empty());

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

        while (!task_queue_.empty() && task_queue_.top().time_ <= now) {
          fit::closure task = std::move(task_queue_.top().task_);
          task_queue_.pop();
          task();
        }

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
  FXL_DCHECK(instance_requestor);
  instance_subscriber_ = instance_requestor;
}

void Mdns::Subscriber::Unsubscribe() {
  if (instance_subscriber_) {
    instance_subscriber_->RemoveSubscriber(this);
    instance_subscriber_ = nullptr;
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
  if (instance_responder_) {
    instance_responder_->Quit();
    instance_responder_ = nullptr;
  }
}

void Mdns::Publisher::Connect(std::shared_ptr<InstanceResponder> instance_responder) {
  FXL_DCHECK(instance_responder);
  instance_responder_ = instance_responder;
}

}  // namespace mdns
