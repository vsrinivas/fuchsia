// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <unordered_set>

#include "garnet/bin/netconnector/mdns/mdns.h"

#include "garnet/bin/netconnector/mdns/address_responder.h"
#include "garnet/bin/netconnector/mdns/dns_formatting.h"
#include "garnet/bin/netconnector/mdns/host_name_resolver.h"
#include "garnet/bin/netconnector/mdns/instance_subscriber.h"
#include "garnet/bin/netconnector/mdns/mdns_addresses.h"
#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "garnet/bin/netconnector/mdns/resource_renewer.h"
#include "garnet/bin/netconnector/mdns/responder.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace netconnector {
namespace mdns {

Mdns::Mdns() : task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {}

Mdns::~Mdns() {}

void Mdns::EnableInterface(const std::string& name, sa_family_t family) {
  transceiver_.EnableInterface(name, family);
}

void Mdns::SetVerbose(bool verbose) {
  verbose_ = verbose;
}

bool Mdns::Start(const std::string& host_name) {
  host_full_name_ = MdnsNames::LocalHostFullName(host_name);

  address_placeholder_ =
      std::make_shared<DnsResource>(host_full_name_, DnsType::kA);

  // Create an address responder agent to respond to simple address queries.
  AddAgent(std::make_shared<AddressResponder>(this, host_full_name_));

  // Create a resource renewer agent to keep resources alive.
  resource_renewer_ = std::make_shared<ResourceRenewer>(this);

  started_ = transceiver_.Start(
      host_full_name_,
      [this](std::unique_ptr<DnsMessage> message,
             const SocketAddress& source_address, uint32_t interface_index) {
        if (verbose_) {
          FXL_LOG(INFO) << "Inbound message from " << source_address
                        << " through interface " << interface_index << ":"
                        << *message;
        }

        for (auto& question : message->questions_) {
          ReceiveQuestion(*question);
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
        for (auto& pair : agents_) {
          pair.second->EndOfMessage();
        }

        SendMessage();
        PostTask();
      });

  if (started_) {
    for (auto pair : agents_) {
      pair.second->Start();
    }

    SendMessage();
    PostTask();
  }

  return started_;
}

void Mdns::Stop() {
  transceiver_.Stop();
  started_ = false;
}

void Mdns::ResolveHostName(const std::string& host_name,
                           fxl::TimePoint timeout,
                           const ResolveHostNameCallback& callback) {
  FXL_DCHECK(MdnsNames::IsValidHostName(host_name));
  FXL_DCHECK(callback);

  AddAgent(
      std::make_shared<HostNameResolver>(this, host_name, timeout, callback));
}

std::shared_ptr<MdnsAgent> Mdns::SubscribeToService(
    const std::string& service_name,
    const ServiceInstanceCallback& callback) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(callback);

  std::shared_ptr<MdnsAgent> agent =
      std::make_shared<InstanceSubscriber>(this, service_name, callback);

  AddAgent(agent);
  return agent;
}

bool Mdns::PublishServiceInstance(const std::string& service_name,
                                  const std::string& instance_name,
                                  IpPort port,
                                  const std::vector<std::string>& text) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  if (instance_publishers_by_instance_full_name_.find(instance_full_name) !=
      instance_publishers_by_instance_full_name_.end()) {
    return false;
  }

  MdnsPublicationPtr publication = MdnsPublication::New();
  publication->port = port.as_uint16_t();
  publication->text = fidl::Array<fidl::String>::From(text);

  std::shared_ptr<MdnsAgent> agent =
      std::make_shared<Responder>(this, host_full_name_, service_name,
                                  instance_name, std::move(publication));

  AddAgent(agent);
  instance_publishers_by_instance_full_name_.emplace(instance_full_name, agent);

  return true;
}

void Mdns::UnpublishServiceInstance(const std::string& service_name,
                                    const std::string& instance_name) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto iter =
      instance_publishers_by_instance_full_name_.find(instance_full_name);

  if (iter != instance_publishers_by_instance_full_name_.end()) {
    iter->second->Quit();
  }
}

bool Mdns::AddResponder(const std::string& service_name,
                        const std::string& instance_name,
                        const std::vector<std::string>& announced_subtypes,
                        fidl::InterfaceHandle<MdnsResponder> responder) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  if (instance_publishers_by_instance_full_name_.find(instance_full_name) !=
      instance_publishers_by_instance_full_name_.end()) {
    return false;
  }

  std::shared_ptr<MdnsAgent> agent = std::make_shared<Responder>(
      this, host_full_name_, service_name, instance_name, announced_subtypes,
      std::move(responder));

  AddAgent(agent);
  instance_publishers_by_instance_full_name_.emplace(instance_full_name, agent);

  return true;
}

void Mdns::WakeAt(std::shared_ptr<MdnsAgent> agent, fxl::TimePoint when) {
  FXL_DCHECK(agent);
  wake_queue_.emplace(when, agent);
}

void Mdns::SendQuestion(std::shared_ptr<DnsQuestion> question) {
  FXL_DCHECK(question);
  outbound_message_.questions_.push_back(question);
}

void Mdns::SendResource(std::shared_ptr<DnsResource> resource,
                        MdnsResourceSection section) {
  FXL_DCHECK(resource);

  switch (section) {
    case MdnsResourceSection::kAnswer:
      outbound_message_.answers_.push_back(resource);
      break;
    case MdnsResourceSection::kAuthority:
      outbound_message_.authorities_.push_back(resource);
      break;
    case MdnsResourceSection::kAdditional:
      outbound_message_.additionals_.push_back(resource);
      break;
    case MdnsResourceSection::kExpired:
      // Expirations are distributed to local agents.
      for (auto& pair : agents_) {
        pair.second->ReceiveResource(*resource, MdnsResourceSection::kExpired);
      }
      break;
  }
}

void Mdns::SendAddresses(MdnsResourceSection section) {
  SendResource(address_placeholder_, section);
}

void Mdns::Renew(const DnsResource& resource) {
  resource_renewer_->Renew(resource);
}

void Mdns::RemoveAgent(MdnsAgent* agent,
                       const std::string& published_instance_full_name) {
  agents_.erase(agent);

  reverse_priority_queue<WakeQueueEntry> temp;
  wake_queue_.swap(temp);

  while (!temp.empty()) {
    if (temp.top().agent_.get() != agent) {
      wake_queue_.emplace(temp.top().time_, temp.top().agent_);
    }

    temp.pop();
  }

  if (!published_instance_full_name.empty()) {
    instance_publishers_by_instance_full_name_.erase(
        published_instance_full_name);
  }

  // In case the agent sent an epitaph.
  SendMessage();
}

void Mdns::AddAgent(std::shared_ptr<MdnsAgent> agent) {
  agents_.emplace(agent.get(), agent);

  if (started_) {
    agent->Start();
    SendMessage();
    PostTask();
  }
}

void Mdns::SendMessage() {
  outbound_message_.UpdateCounts();

  if (outbound_message_.questions_.empty()) {
    outbound_message_.header_.SetResponse(true);
    outbound_message_.header_.SetAuthoritativeAnswer(true);
  }

  if (verbose_) {
    FXL_LOG(INFO) << "Outbound message: " << outbound_message_;
  }

  transceiver_.SendMessage(&outbound_message_, MdnsAddresses::kV4Multicast, 0);

  // Reset |outbound_message_|.
  // TODO(dalesat): Replace this awkwardness when unicast reply is implemented.
  outbound_message_.header_.flags_ = 0;
  outbound_message_.questions_.clear();
  outbound_message_.answers_.clear();
  outbound_message_.authorities_.clear();
  outbound_message_.additionals_.clear();
}

void Mdns::ReceiveQuestion(const DnsQuestion& question) {
  // Renewer doesn't need questions.
  for (auto& pair : agents_) {
    pair.second->ReceiveQuestion(question);
  }
}

void Mdns::ReceiveResource(const DnsResource& resource,
                           MdnsResourceSection section) {
  // Renewer is always first.
  resource_renewer_->ReceiveResource(resource, section);
  for (auto& pair : agents_) {
    pair.second->ReceiveResource(resource, section);
  }
}

void Mdns::PostTask() {
  fxl::TimePoint when = fxl::TimePoint::Max();

  if (!wake_queue_.empty()) {
    when = wake_queue_.top().time_;
  }

  if (when == fxl::TimePoint::Max()) {
    return;
  }

  if (!post_task_queue_.empty() && post_task_queue_.top() <= when) {
    // We're already scheduled to wake up by |when|.
    return;
  }

  post_task_queue_.push(when);

  task_runner_->PostTaskForTime(
      [this, when]() {
        while (!post_task_queue_.empty() && post_task_queue_.top() <= when) {
          post_task_queue_.pop();
        }

        fxl::TimePoint now = fxl::TimePoint::Now();

        while (!wake_queue_.empty() && wake_queue_.top().time_ <= now) {
          std::shared_ptr<MdnsAgent> agent = wake_queue_.top().agent_;
          wake_queue_.pop();
          agent->Wake();
        }

        SendMessage();
        PostTask();
      },
      when);
}

}  // namespace mdns
}  // namespace netconnector
