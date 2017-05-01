// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/instance_publisher.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

InstancePublisher::InstancePublisher(MdnsAgent::Host* host,
                                     const std::string& host_full_name,
                                     const std::string& instance_full_name,
                                     const std::string& service_full_name,
                                     IpPort port,
                                     const std::vector<std::string>& text)
    : host_(host),
      instance_full_name_(instance_full_name),
      service_full_name_(service_full_name),
      answer_(
          std::make_shared<DnsResource>(service_full_name_, DnsType::kPtr)) {
  answer_->ptr_.pointer_domain_name_ = instance_full_name_;

  additionals_.push_back(
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv));
  additionals_.back()->srv_.port_ = port;
  additionals_.back()->srv_.target_ = host_full_name;

  additionals_.push_back(
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kTxt));
  additionals_.back()->txt_.strings_ = text;
}

InstancePublisher::~InstancePublisher() {}

void InstancePublisher::Start() {
  SendRecords(ftl::TimePoint::Now());
  SendRecords(ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(1));
  SendRecords(ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(3));
  SendRecords(ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(7));
}

void InstancePublisher::Wake() {}

void InstancePublisher::ReceiveQuestion(const DnsQuestion& question) {
  if (question.type_ == DnsType::kPtr &&
      question.name_.dotted_string_ == service_full_name_) {
    SendRecords(ftl::TimePoint::Now());
  }
}

void InstancePublisher::ReceiveResource(const DnsResource& resource,
                                        MdnsResourceSection section) {}

void InstancePublisher::EndOfMessage() {}

void InstancePublisher::Quit() {
  answer_->time_to_live_ = 0;

  for (auto& additional : additionals_) {
    additional->time_to_live_ = 0;
  }

  SendRecords(ftl::TimePoint::Now());

  host_->RemoveAgent(service_full_name_);
}

void InstancePublisher::SendRecords(ftl::TimePoint when) {
  // We schedule these a nanosecond apart to ensure proper sequence.
  int64_t sequence = 0;

  host_->SendResource(answer_, MdnsResourceSection::kAnswer, when);

  for (auto& additional : additionals_) {
    host_->SendResource(additional, MdnsResourceSection::kAdditional,
                        when + ftl::TimeDelta::FromNanoseconds(++sequence));
  }

  host_->SendAddresses(MdnsResourceSection::kAdditional,
                       when + ftl::TimeDelta::FromNanoseconds(++sequence));
}

}  // namespace mdns
}  // namespace netconnector
