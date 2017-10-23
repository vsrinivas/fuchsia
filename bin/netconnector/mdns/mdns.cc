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
      host_full_name_, [this](std::unique_ptr<DnsMessage> message,
                              const ReplyAddress& reply_address) {
        if (verbose_) {
          FXL_LOG(INFO) << "Inbound message from " << reply_address << ":"
                        << *message;
        }

        for (auto& question : message->questions_) {
          // We reply to questions using unicast if specifically requested in
          // the question or if the sender's port isn't 5353.
          ReceiveQuestion(*question, (question->unicast_response_ ||
                                      reply_address.socket_address().port() !=
                                          MdnsAddresses::kMdnsPort)
                                         ? reply_address
                                         : MdnsAddresses::kV4MulticastReply);
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

        SendMessages();
      });

  if (started_) {
    for (auto pair : agents_) {
      pair.second->Start();
    }

    SendMessages();
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

  std::shared_ptr<Responder> agent =
      std::make_shared<Responder>(this, host_full_name_, service_name,
                                  instance_name, std::move(publication));

  AddAgent(agent);
  instance_publishers_by_instance_full_name_.emplace(instance_full_name, agent);

  return true;
}

bool Mdns::UnpublishServiceInstance(const std::string& service_name,
                                    const std::string& instance_name) {
  FXL_DCHECK(MdnsNames::IsValidServiceName(service_name));
  FXL_DCHECK(MdnsNames::IsValidInstanceName(instance_name));

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto iter =
      instance_publishers_by_instance_full_name_.find(instance_full_name);

  if (iter == instance_publishers_by_instance_full_name_.end()) {
    return false;
  }

  iter->second->Quit();

  return true;
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

  std::shared_ptr<Responder> agent = std::make_shared<Responder>(
      this, host_full_name_, service_name, instance_name, announced_subtypes,
      std::move(responder));

  AddAgent(agent);
  instance_publishers_by_instance_full_name_.emplace(instance_full_name, agent);

  return true;
}

void Mdns::PostTaskForTime(MdnsAgent* agent,
                           fxl::Closure task,
                           fxl::TimePoint target_time) {
  task_queue_.emplace(agent, task, target_time);
  PostTask();
}

void Mdns::SendQuestion(std::shared_ptr<DnsQuestion> question) {
  FXL_DCHECK(question);
  DnsMessage& message =
      outbound_messages_by_reply_address_[MdnsAddresses::kV4MulticastReply];
  message.questions_.push_back(question);
}

void Mdns::SendResource(std::shared_ptr<DnsResource> resource,
                        MdnsResourceSection section,
                        const ReplyAddress& reply_address) {
  FXL_DCHECK(resource);

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
      // Expirations are distributed to local agents.
      for (auto& pair : agents_) {
        pair.second->ReceiveResource(*resource, MdnsResourceSection::kExpired);
      }
      break;
  }
}

void Mdns::SendAddresses(MdnsResourceSection section,
                         const ReplyAddress& reply_address) {
  SendResource(address_placeholder_, section, reply_address);
}

void Mdns::Renew(const DnsResource& resource) {
  resource_renewer_->Renew(resource);
}

void Mdns::RemoveAgent(const MdnsAgent* agent,
                       const std::string& published_instance_full_name) {
  agents_.erase(agent);

  // Remove all pending tasks posted by this agent.
  std::priority_queue<TaskQueueEntry> temp;
  task_queue_.swap(temp);

  while (!temp.empty()) {
    if (temp.top().agent_ != agent) {
      task_queue_.emplace(temp.top().agent_, temp.top().task_,
                          temp.top().time_);
    }

    temp.pop();
  }

  if (!published_instance_full_name.empty()) {
    instance_publishers_by_instance_full_name_.erase(
        published_instance_full_name);
  }

  // In case the agent sent an epitaph.
  SendMessages();
}

void Mdns::AddAgent(std::shared_ptr<MdnsAgent> agent) {
  agents_.emplace(agent.get(), agent);

  if (started_) {
    agent->Start();
    SendMessages();
  }
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

    if (verbose_) {
      if (reply_address == MdnsAddresses::kV4MulticastReply) {
        FXL_LOG(INFO) << "Outbound message (multicast): " << message;
      } else {
        FXL_LOG(INFO) << "Outbound message to " << reply_address << ":"
                      << message;
      }
    }

    transceiver_.SendMessage(&message, reply_address);
  }

  outbound_messages_by_reply_address_.clear();
}

void Mdns::ReceiveQuestion(const DnsQuestion& question,
                           const ReplyAddress& reply_address) {
  // Renewer doesn't need questions.
  for (auto& pair : agents_) {
    pair.second->ReceiveQuestion(question, reply_address);
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
  FXL_DCHECK(!task_queue_.empty());

  if (task_queue_.top().time_ >= posted_task_time_) {
    return;
  }

  posted_task_time_ = task_queue_.top().time_;

  task_runner_->PostTaskForTime(
      [this]() {
        // Suppress recursive calls to this method.
        posted_task_time_ = fxl::TimePoint::Min();

        fxl::TimePoint now = fxl::TimePoint::Now();

        while (!task_queue_.empty() && task_queue_.top().time_ <= now) {
          fxl::Closure task = task_queue_.top().task_;
          task_queue_.pop();
          task();
        }

        SendMessages();

        posted_task_time_ = fxl::TimePoint::Max();
        if (!task_queue_.empty()) {
          PostTask();
        }
      },
      posted_task_time_);
}

}  // namespace mdns
}  // namespace netconnector
