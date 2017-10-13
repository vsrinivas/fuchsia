// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/instance_publisher.h"

#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

InstancePublisher::InstancePublisher(MdnsAgent::Host* host,
                                     const std::string& host_full_name,
                                     const std::string& service_name,
                                     const std::string& instance_name,
                                     IpPort port,
                                     const std::vector<std::string>& text)
    : host_(host),
      instance_full_name_(
          MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      service_full_name_(MdnsNames::LocalServiceFullName(service_name)),
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
  SendRecords(fxl::TimePoint::Now());
  SendRecords(fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(1));
  SendRecords(fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(3));
  SendRecords(fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(7));
}

void InstancePublisher::Wake() {}

void InstancePublisher::ReceiveQuestion(const DnsQuestion& question) {
  switch (question.type_) {
    case DnsType::kPtr:
      if (question.name_.dotted_string_ == service_full_name_) {
        SendRecords(fxl::TimePoint::Now());
      }
      break;
    case DnsType::kSrv:
    case DnsType::kTxt:
      if (question.name_.dotted_string_ == instance_full_name_) {
        SendRecords(fxl::TimePoint::Now());
      }
      break;
    default:
      break;
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

  SendRecords(fxl::TimePoint::Now());

  host_->RemoveAgent(this, instance_full_name_);
}

void InstancePublisher::SendRecords(fxl::TimePoint when) {
  // We schedule these a nanosecond apart to ensure proper sequence.
  int64_t sequence = 0;

  host_->SendResource(answer_, MdnsResourceSection::kAnswer, when);

  for (auto& additional : additionals_) {
    host_->SendResource(additional, MdnsResourceSection::kAdditional,
                        when + fxl::TimeDelta::FromNanoseconds(++sequence));
  }

  host_->SendAddresses(MdnsResourceSection::kAdditional,
                       when + fxl::TimeDelta::FromNanoseconds(++sequence));
}

}  // namespace mdns
}  // namespace netconnector
