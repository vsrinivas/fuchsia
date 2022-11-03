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

#include "src/connectivity/network/mdns/service/agents/address_prober.h"
#include "src/connectivity/network/mdns/service/agents/address_responder.h"
#include "src/connectivity/network/mdns/service/agents/host_name_requestor.h"
#include "src/connectivity/network/mdns/service/agents/host_name_resolver.h"
#include "src/connectivity/network/mdns/service/agents/instance_prober.h"
#include "src/connectivity/network/mdns/service/agents/instance_requestor.h"
#include "src/connectivity/network/mdns/service/agents/instance_responder.h"
#include "src/connectivity/network/mdns/service/agents/resource_renewer.h"
#include "src/connectivity/network/mdns/service/agents/service_instance_resolver.h"
#include "src/connectivity/network/mdns/service/common/formatters.h"
#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/encoding/dns_formatting.h"

namespace mdns {

Mdns::Mdns(Transceiver& transceiver)
    : dispatcher_(async_get_default_dispatcher()), transceiver_(transceiver) {}

Mdns::~Mdns() {}

void Mdns::SetVerbose(bool verbose) {
#ifdef MDNS_TRACE
  verbose_ = verbose;
#endif  // MDNS_TRACE
}

void Mdns::Start(fuchsia::net::interfaces::WatcherPtr interfaces_watcher,
                 const std::string& local_host_name, bool perform_address_probe,
                 fit::closure ready_callback, std::vector<std::string> alt_services) {
  FX_DCHECK(!local_host_name.empty());
  FX_DCHECK(ready_callback);
  FX_DCHECK(state_ == State::kNotStarted);

  ready_callback_ = std::move(ready_callback);
  state_ = State::kWaitingForInterfaces;

  original_local_host_name_ = local_host_name;

  alt_services_ = std::move(alt_services);

  // Create a resource renewer agent to keep resources alive.
  resource_renewer_ = std::make_shared<ResourceRenewer>(this);

  // Create an address responder agent to respond to address queries.
  AddAgent(std::make_shared<AddressResponder>(this, Media::kBoth, IpVersions::kBoth));

  // If alternate services are registered, create an address responder agent to respond to address
  // queries for the alternate host name. No probe is performed.
  // TODO(fxb/113901): Remove this when alt_services is no longer needed.
  if (!alt_services_.empty()) {
    auto alt_host_name = MdnsNames::AltHostName(local_host_name);
    if (alt_host_name == local_host_name) {
      FX_LOGS(ERROR) << "Unexpected host name format, cannot generate alternate host name.";
    }

    AddAgent(std::make_shared<AddressResponder>(this, MdnsNames::HostFullName(alt_host_name),
                                                std::vector<inet::IpAddress>{}, Media::kBoth,
                                                IpVersions::kBoth));
  }

  transceiver_.Start(
      std::move(interfaces_watcher),
      [this, perform_address_probe]() {
        // TODO(dalesat): Link changes that create host name conflicts.
        // Once we have a NIC and we've decided on a unique host name, we
        // don't do any more address probes. This means that we could have link
        // changes that cause two hosts with the same name to be on the same
        // subnet. To improve matters, we need to be prepared to change a host
        // name we've been using for a while.
        if (state_ == State::kWaitingForInterfaces && transceiver_.HasInterfaces()) {
          OnInterfacesStarted(original_local_host_name_, perform_address_probe);
        }

        if (state_ == State::kActive) {
          for (const auto& agent : agents_) {
            agent->OnLocalHostAddressesChanged();
          }
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

        for (const auto& question : message->questions_) {
          // We reply to questions using unicast if specifically requested in
          // the question or if the sender's port isn't 5353.
          ReceiveQuestion(*question,
                          (question->unicast_response_ ||
                           reply_address.socket_address().port() != MdnsAddresses::port())
                              ? reply_address
                              : ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                          reply_address);
        }

        for (const auto& resource : message->answers_) {
          ReceiveResource(*resource, MdnsResourceSection::kAnswer, reply_address);
        }

        for (const auto& resource : message->authorities_) {
          ReceiveResource(*resource, MdnsResourceSection::kAuthority, reply_address);
        }

        for (const auto& resource : message->additionals_) {
          ReceiveResource(*resource, MdnsResourceSection::kAdditional, reply_address);
        }

        resource_renewer_->EndOfMessage();
        DPROHIBIT_AGENT_REMOVAL();
        for (const auto& agent : agents_) {
          agent->EndOfMessage();
        }
        DALLOW_AGENT_REMOVAL();

        defer_flush_ = false;

        SendMessages();
      },
      MdnsInterfaceTransceiver::Create);

  // The interface monitor may have already found interfaces. In that case,
  // start the address probe in case we don't get any link change notifications.
  if (state_ == State::kWaitingForInterfaces && transceiver_.HasInterfaces()) {
    OnInterfacesStarted(original_local_host_name_, perform_address_probe);
  }
}

void Mdns::Stop() {
  transceiver_.Stop();
  ready_callback_ = nullptr;
  state_ = State::kNotStarted;
}

void Mdns::ResolveHostName(const std::string& host_name, zx::duration timeout, Media media,
                           IpVersions ip_versions, bool include_local, bool include_local_proxies,
                           ResolveHostNameCallback callback) {
  FX_DCHECK(MdnsNames::IsValidHostName(host_name));
  FX_DCHECK(callback);
  FX_DCHECK(state_ == State::kActive);

  auto agent =
      std::make_shared<HostNameResolver>(this, host_name, media, ip_versions, include_local,
                                         include_local_proxies, timeout, std::move(callback));
  AddAgent(agent);
}

void Mdns::SubscribeToHostName(const std::string& host_name, Media media, IpVersions ip_versions,
                               bool include_local, bool include_local_proxies,
                               HostNameSubscriber* subscriber) {
  FX_DCHECK(MdnsNames::IsValidHostName(host_name));
  FX_DCHECK(subscriber);
  FX_DCHECK(state_ == State::kActive);

  std::shared_ptr<HostNameRequestor> agent;
  RequestorKey key(host_name, media, ip_versions);

  auto iter = host_name_requestors_by_key_.find(key);
  if (iter == host_name_requestors_by_key_.end()) {
    agent = std::make_shared<HostNameRequestor>(this, host_name, media, ip_versions, include_local,
                                                include_local_proxies);

    host_name_requestors_by_key_.emplace(key, agent);
    agent->SetOnQuitCallback([this, key]() { host_name_requestors_by_key_.erase(key); });

    subscriber->Connect(agent);

    // Add the subscriber before calling AddAgent (which starts the agent).
    agent->AddSubscriber(subscriber);
    AddAgent(agent);
  } else {
    agent = iter->second;
    subscriber->Connect(agent);
    agent->AddSubscriber(subscriber);
  }
}

void Mdns::ResolveServiceInstance(const std::string& service, const std::string& instance,
                                  zx::time timeout, Media media, IpVersions ip_versions,
                                  bool include_local, bool include_local_proxies,
                                  ResolveServiceInstanceCallback callback) {
  FX_DCHECK(!service.empty());
  FX_DCHECK(!instance.empty());
  FX_DCHECK(callback);
  FX_DCHECK(state_ == State::kActive);

  AddAgent(std::make_shared<ServiceInstanceResolver>(this, service, instance, timeout, media,
                                                     ip_versions, include_local,
                                                     include_local_proxies, std::move(callback)));
}

void Mdns::SubscribeToService(const std::string& service_name, Media media, IpVersions ip_versions,
                              bool include_local, bool include_local_proxies,
                              Subscriber* subscriber) {
  FX_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FX_DCHECK(subscriber);
  FX_DCHECK(state_ == State::kActive);

  std::shared_ptr<InstanceRequestor> agent;
  RequestorKey key(service_name, media, ip_versions);

  auto iter = instance_requestors_by_key_.find(key);
  if (iter == instance_requestors_by_key_.end()) {
    agent = std::make_shared<InstanceRequestor>(this, service_name, media, ip_versions,
                                                include_local, include_local_proxies);

    instance_requestors_by_key_.emplace(key, agent);
    agent->SetOnQuitCallback([this, key]() { instance_requestors_by_key_.erase(key); });

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

void Mdns::SubscribeToAllServices(Media media, IpVersions ip_versions, bool include_local,
                                  bool include_local_proxies, Subscriber* subscriber) {
  FX_DCHECK(subscriber);
  FX_DCHECK(state_ == State::kActive);

  std::shared_ptr<InstanceRequestor> agent;
  RequestorKey key("", media, ip_versions);

  auto iter = instance_requestors_by_key_.find(key);
  if (iter == instance_requestors_by_key_.end()) {
    agent = std::make_shared<InstanceRequestor>(this, media, ip_versions, include_local,
                                                include_local_proxies);

    instance_requestors_by_key_.emplace(key, agent);
    agent->SetOnQuitCallback([this, key]() { instance_requestors_by_key_.erase(key); });

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

bool Mdns::PublishServiceInstance(std::string host_name, std::vector<inet::IpAddress> addresses,
                                  std::string service_name, std::string instance_name, Media media,
                                  IpVersions ip_versions, bool perform_probe,
                                  Publisher* publisher) {
  FX_DCHECK(host_name.empty() == addresses.empty());
  FX_DCHECK(host_name.empty() || MdnsNames::IsValidHostName(host_name));
  FX_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FX_DCHECK(MdnsNames::IsValidInstanceName(instance_name));
  FX_DCHECK(publisher);
  FX_DCHECK(state_ == State::kActive);

  std::string instance_full_name = MdnsNames::InstanceFullName(instance_name, service_name);

  if (instance_responders_by_instance_full_name_.find(instance_full_name) !=
      instance_responders_by_instance_full_name_.end()) {
    return false;
  }

  // If a host name was provided, the instance is to be published by a proxy host.
  bool from_proxy = !host_name.empty();

  // If we're not publishing from a proxy host, and the service type is in the list of alternate
  // services, publish from the alternate host name.
  if (!from_proxy &&
      std::find(alt_services_.begin(), alt_services_.end(), service_name) != alt_services_.end()) {
    // TODO(fxb/113901): Remove this when alt_services is no longer needed.
    FX_LOGS(INFO) << "Alternate services specified, responding on alternate host name.";
    host_name = MdnsNames::AltHostName(original_local_host_name_);
  }

  auto agent = std::make_shared<InstanceResponder>(this, host_name, addresses, service_name,
                                                   instance_name, media, ip_versions, publisher);

  instance_responders_by_instance_full_name_.emplace(instance_full_name, agent);
  agent->SetOnQuitCallback([this, instance_full_name, service_name, instance_name, from_proxy]() {
    instance_responders_by_instance_full_name_.erase(instance_full_name);
    OnRemoveLocalServiceInstance(service_name, instance_name, from_proxy);
  });

  publisher->Connect(agent);

  if (perform_probe) {
    // We're using a bogus port number here, which is OK, because the 'proposed'
    // resource created from it is only used for collision resolution.
    auto prober = std::make_shared<InstanceProber>(
        this, service_name, instance_name,
        host_name.empty() ? local_host_full_name_ : MdnsNames::HostFullName(host_name),
        inet::IpPort::From_uint16_t(0), media, ip_versions,
        [this, instance_full_name, agent, publisher, from_proxy](bool successful) {
          publisher->DisconnectProber();

          if (!successful) {
            publisher->ReportSuccess(false);
            instance_responders_by_instance_full_name_.erase(instance_full_name);
            return;
          }

          publisher->ReportSuccess(true);
          AddAgent(agent);
          if (state_ == State::kActive) {
            auto service_instance = agent->service_instance();
            if (service_instance) {
              OnAddLocalServiceInstance(*service_instance, from_proxy);
            }
          }
        });

    AddAgent(prober);
    publisher->ConnectProber(prober);
  } else {
    publisher->ReportSuccess(true);
    AddAgent(agent);
  }

  return true;
}

bool Mdns::PublishHost(std::string host_name, std::vector<inet::IpAddress> addresses, Media media,
                       IpVersions ip_versions, bool perform_probe, HostPublisher* publisher) {
  FX_DCHECK(MdnsNames::IsValidHostName(host_name));
  FX_DCHECK(!addresses.empty());
  FX_DCHECK(publisher);
  FX_DCHECK(state_ == State::kActive);

  std::string host_full_name = MdnsNames::HostFullName(std::move(host_name));

  if (host_full_name == local_host_full_name_) {
    // Publication of the local host doesn't use this method (for now), so we check separately
    // that the supplied |host_name| doesn't conflict with the local host's name.
    return false;
  }

  if (address_responders_by_host_full_name_.find(host_full_name) !=
      address_responders_by_host_full_name_.end()) {
    return false;
  }

  auto agent =
      std::make_shared<AddressResponder>(this, host_full_name, addresses, media, ip_versions);

  address_responders_by_host_full_name_.emplace(host_full_name, agent);
  agent->SetOnQuitCallback([this, host_full_name]() {
    address_responders_by_host_full_name_.erase(host_full_name);
    OnRemoveProxyHost(host_full_name);
  });

  publisher->Connect(agent);

  if (perform_probe) {
    auto prober = std::make_shared<AddressProber>(
        this, host_full_name, addresses, media, ip_versions,
        [this, host_full_name, agent, publisher](bool successful) {
          publisher->DisconnectProber();

          if (!successful) {
            publisher->ReportSuccess(false);
            address_responders_by_host_full_name_.erase(host_full_name);
            return;
          }

          publisher->ReportSuccess(true);
          AddAgent(agent);
          if (state_ == State::kActive) {
            OnAddProxyHost(host_full_name, agent->addresses());
          }
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

void Mdns::OnInterfacesStarted(const std::string& local_host_name, bool perform_address_probe) {
  if (perform_address_probe) {
    StartAddressProbe(local_host_name);
    return;
  }

  RegisterLocalHostName(local_host_name);
  OnReady();
}

void Mdns::StartAddressProbe(const std::string& local_host_name) {
  state_ = State::kAddressProbeInProgress;

  RegisterLocalHostName(local_host_name);
  std::cout << "mDNS: Verifying uniqueness of host name " << local_host_full_name_ << "\n";

  // Create an address prober to look for host name conflicts. The address
  // prober removes itself immediately before it calls the callback.
  auto address_prober = std::make_shared<AddressProber>(
      this, Media::kBoth, IpVersions::kBoth, [this](bool successful) {
        FX_DCHECK(agents_.empty());

        if (!successful) {
          std::cout << "mDNS: Another host is using name " << local_host_full_name_ << "\n";
          OnHostNameConflict();
          return;
        }

        OnReady();
      });

  // We don't use |AddAgent| here, because agents added that way don't
  // actually participate until we're done probing for host name conflicts.
  agents_.emplace(address_prober);
  address_prober->Start(local_host_full_name_);
  SendMessages();
}

void Mdns::RegisterLocalHostName(const std::string& local_host_name) {
  local_host_name_ = local_host_name;
  local_host_full_name_ = MdnsNames::HostFullName(local_host_name);
  address_placeholder_ = std::make_shared<DnsResource>(local_host_full_name_, DnsType::kA);
}

void Mdns::OnReady() {
  std::cout << "mDNS: Using unique host name " << local_host_full_name_ << "\n";

  // Start all the agents.
  state_ = State::kActive;

  // |resource_renewer_| doesn't need to be started, but we do it
  // anyway in case that changes.
  resource_renewer_->Start(local_host_full_name_);

  for (const auto& agent : agents_awaiting_start_) {
    AddAgent(agent);
  }

  agents_awaiting_start_.clear();

  // Let the client know we're ready.
  FX_DCHECK(ready_callback_);
  ready_callback_();
  ready_callback_ = nullptr;
}

void Mdns::OnAddProxyHost(const std::string& host_full_name,
                          const std::vector<HostAddress>& addresses) {
  for (const auto& agent : agents_) {
    agent->OnAddProxyHost(host_full_name, addresses);
  }
}

void Mdns::OnRemoveProxyHost(const std::string& host_full_name) {
  for (const auto& agent : agents_) {
    agent->OnRemoveProxyHost(host_full_name);
  }
}

void Mdns::OnAddLocalServiceInstance(const ServiceInstance& service_instance, bool from_proxy) {
  for (const auto& agent : agents_) {
    agent->OnAddLocalServiceInstance(service_instance, from_proxy);
  }
}

void Mdns::OnChangeLocalServiceInstance(const ServiceInstance& service_instance, bool from_proxy) {
  for (const auto& agent : agents_) {
    agent->OnChangeLocalServiceInstance(service_instance, from_proxy);
  }
}

void Mdns::OnRemoveLocalServiceInstance(const std::string& service_name,
                                        const std::string& instance_name, bool from_proxy) {
  for (const auto& agent : agents_) {
    agent->OnRemoveLocalServiceInstance(service_name, instance_name, from_proxy);
  }
}

void Mdns::OnHostNameConflict() {
  // TODO(dalesat): Support other renaming strategies?
  std::ostringstream os;
  os << original_local_host_name_ << next_local_host_name_deduplicator_;
  ++next_local_host_name_deduplicator_;

  StartAddressProbe(os.str());
}

zx::time Mdns::now() { return zx::clock::get_monotonic(); }

void Mdns::PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) {
  task_queue_.emplace(agent, std::move(task), target_time);
  PostTask();
}

void Mdns::SendQuestion(std::shared_ptr<DnsQuestion> question, ReplyAddress reply_address) {
  FX_DCHECK(question);
  outbound_message_builders_by_reply_address_[reply_address].AddQuestion(question);
}

void Mdns::SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                        const ReplyAddress& reply_address) {
  FX_DCHECK(resource);
  // |reply_address| should never be the V6 multicast address, because the V4 multicast address
  // is used to indicate a multicast reply.
  FX_DCHECK(reply_address.socket_address() != MdnsAddresses::v6_multicast());

  if (section == MdnsResourceSection::kExpired) {
    // Expirations are distributed to local agents. We handle this case
    // separately so we don't create an empty outbound message.
    prohibit_agent_removal_ = true;

    for (const auto& agent : agents_) {
      agent->ReceiveResource(*resource, MdnsResourceSection::kExpired, ReplyAddress());
    }

    prohibit_agent_removal_ = false;
    return;
  }

  outbound_message_builders_by_reply_address_[reply_address].AddResource(resource, section);
}

void Mdns::SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) {
  SendResource(address_placeholder_, section, reply_address);
}

void Mdns::Renew(const DnsResource& resource, Media media, IpVersions ip_versions) {
  resource_renewer_->Renew(resource, media, ip_versions);
}

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

void Mdns::AddLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) {
  OnAddLocalServiceInstance(instance, from_proxy);
}

void Mdns::ChangeLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) {
  OnChangeLocalServiceInstance(instance, from_proxy);
}

std::vector<HostAddress> Mdns::LocalHostAddresses() { return transceiver_.LocalHostAddresses(); }

void Mdns::AddAgent(std::shared_ptr<MdnsAgent> agent) {
  if (state_ == State::kActive) {
    agents_.emplace(agent);
    FX_DCHECK(!local_host_full_name_.empty());
    agent->Start(local_host_full_name_);

    // Notify the agent of all current local proxies.
    for (auto& pair : address_responders_by_host_full_name_) {
      agent->OnAddProxyHost(pair.first, pair.second->addresses());
    }

    // Notify the agent of all current local instances.
    for (auto& pair : instance_responders_by_instance_full_name_) {
      auto service_instance = pair.second->service_instance();
      if (service_instance) {
        auto from_proxy = pair.second->from_proxy();
        agent->OnAddLocalServiceInstance(*service_instance, from_proxy);
      }
    }

    SendMessages();
  } else {
    agents_awaiting_start_.push_back(agent);
  }
}

void Mdns::SendMessages() {
  for (const auto& [reply_address, builder] : outbound_message_builders_by_reply_address_) {
    DnsMessage message;
    builder.Build(message);

#ifdef MDNS_TRACE
    if (verbose_) {
      std::ostringstream os;

      if (reply_address.is_multicast_placeholder()) {
        os << "(multicast)";
      } else {
        os << "to " << reply_address;
      }

      switch (reply_address.media()) {
        case Media::kWired:
          os << ", wired only";
          break;
        case Media::kWireless:
          os << ", wireless only";
          break;
        case Media::kBoth:
          break;
      }

      switch (reply_address.ip_versions()) {
        case IpVersions::kV4:
          os << ", V4 only";
          break;
        case IpVersions::kV6:
          os << ", V6 only";
          break;
        case IpVersions::kBoth:
          break;
      }

      FX_LOGS(INFO) << "Outbound message " << os.str() << ":" << message;
    }
#endif  // MDNS_TRACE

    transceiver_.SendMessage(std::move(message), reply_address);
  }

  outbound_message_builders_by_reply_address_.clear();
}

void Mdns::ReceiveQuestion(const DnsQuestion& question, const ReplyAddress& reply_address,
                           const ReplyAddress& sender_address) {
  // |reply_address| should never be the V6 multicast address, because the V4 multicast address
  // is used to indicate a multicast reply.
  FX_DCHECK(reply_address.socket_address() != MdnsAddresses::v6_multicast());

  // Renewer doesn't need questions.
  DPROHIBIT_AGENT_REMOVAL();
  for (const auto& agent : agents_) {
    agent->ReceiveQuestion(question, reply_address, sender_address);
  }

  DALLOW_AGENT_REMOVAL();
}

void Mdns::ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                           ReplyAddress sender_address) {
  // Renewer is always first.
  resource_renewer_->ReceiveResource(resource, section, sender_address);
  DPROHIBIT_AGENT_REMOVAL();
  for (const auto& agent : agents_) {
    agent->ReceiveResource(resource, section, sender_address);
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
std::unique_ptr<Mdns::Publication> Mdns::Publication::Create(
    inet::IpPort port, const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
    uint16_t srv_weight) {
  auto publication = std::make_unique<Publication>();
  publication->port_ = port;
  publication->text_ = text;
  publication->srv_priority_ = srv_priority;
  publication->srv_weight_ = srv_weight;
  return publication;
}

std::unique_ptr<Mdns::Publication> Mdns::Publication::Clone() const {
  auto result = Create(port_, text_, srv_priority_, srv_weight_);
  result->ptr_ttl_seconds_ = ptr_ttl_seconds_;
  result->srv_ttl_seconds_ = srv_ttl_seconds_;
  result->txt_ttl_seconds_ = txt_ttl_seconds_;
  return result;
}

///////////////////////////////////////////////////////////////////////////////

Mdns::HostNameSubscriber::~HostNameSubscriber() { Unsubscribe(); }

void Mdns::HostNameSubscriber::Connect(std::shared_ptr<HostNameRequestor> host_name_requestor) {
  FX_DCHECK(host_name_requestor);
  host_name_requestor_ = host_name_requestor;
}

void Mdns::HostNameSubscriber::Unsubscribe() {
  if (host_name_requestor_) {
    host_name_requestor_->RemoveSubscriber(this);
    host_name_requestor_ = nullptr;
  }
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

///////////////////////////////////////////////////////////////////////////////

Mdns::HostPublisher::~HostPublisher() { Unpublish(); }

void Mdns::HostPublisher::Unpublish() {
  if (address_prober_) {
    address_prober_->Quit();
    address_prober_ = nullptr;
  }

  if (address_responder_) {
    address_responder_->Quit();
    address_responder_ = nullptr;
  }
}

void Mdns::HostPublisher::Connect(std::shared_ptr<AddressResponder> address_responder) {
  FX_DCHECK(address_responder);
  address_responder_ = address_responder;
}

void Mdns::HostPublisher::ConnectProber(std::shared_ptr<AddressProber> address_prober) {
  FX_DCHECK(address_prober);
  address_prober_ = address_prober;
}

void Mdns::HostPublisher::DisconnectProber() {
  FX_DCHECK(address_prober_);
  address_prober_ = nullptr;
}

}  // namespace mdns
