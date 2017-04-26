// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/instance_subscriber.h"

#include "apps/netconnector/src/mdns/mdns_names.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

InstanceSubscriber::InstanceSubscriber(MdnsAgent::Host* host,
                                       const std::string& service_name,
                                       const std::string& service_full_name,
                                       const ServiceInstanceCallback& callback)
    : host_(host),
      service_name_(service_name),
      service_full_name_(service_full_name),
      callback_(callback),
      question_(
          std::make_shared<DnsQuestion>(service_full_name_, DnsType::kPtr)) {
  FTL_DCHECK(callback_);
}

InstanceSubscriber::~InstanceSubscriber() {}

void InstanceSubscriber::Start() {
  Wake();
}

void InstanceSubscriber::Wake() {
  host_->SendQuestion(question_, ftl::TimePoint::Now());

  if (query_delay_ == 0) {
    query_delay_ = 1;
  } else {
    query_delay_ *= 2;
    if (query_delay_ > kMaxQueryInterval) {
      query_delay_ = kMaxQueryInterval;
    }
  }

  host_->WakeAt(
      shared_from_this(),
      ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(query_delay_));
}

void InstanceSubscriber::ReceiveQuestion(const DnsQuestion& question) {}

void InstanceSubscriber::ReceiveResource(const DnsResource& resource,
                                         MdnsResourceSection section) {
  switch (resource.type_) {
    case DnsType::kPtr:
      if (resource.name_.dotted_string_ == service_full_name_) {
        ReceivePtrResource(resource, section);
      }
      break;
    case DnsType::kSrv: {
      InstanceInfo* instance_info =
          FindInstanceInfo(resource.name_.dotted_string_);
      if (instance_info != nullptr) {
        ReceiveSrvResource(resource, section, instance_info);
      }
    } break;
    case DnsType::kTxt: {
      InstanceInfo* instance_info =
          FindInstanceInfo(resource.name_.dotted_string_);
      if (instance_info != nullptr) {
        ReceiveTxtResource(resource, section, instance_info);
      }
    } break;
    case DnsType::kA: {
      auto iter = target_infos_by_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_name_.end()) {
        ReceiveAResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kAaaa: {
      auto iter = target_infos_by_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_name_.end()) {
        ReceiveAaaaResource(resource, section, &iter->second);
      }
    } break;
    default:
      break;
  }
}

void InstanceSubscriber::EndOfMessage() {
  for (auto& pair : instance_infos_by_name_) {
    if (pair.second.target_.empty()) {
      // We haven't yet seen an SRV record for this instance.
      continue;
    }

    auto iter = target_infos_by_name_.find(pair.second.target_);
    FTL_DCHECK(iter != target_infos_by_name_.end());
    if (!pair.second.dirty_ && !iter->second.dirty_) {
      // Both the instance info and target info are clean.
      continue;
    }

    if (!iter->second.v4_address_ && !iter->second.v6_address_) {
      // No addresses yet.
      continue;
    }

    // Something has changed.
    callback_(service_name_, pair.first,
              SocketAddress(iter->second.v4_address_, pair.second.port_),
              SocketAddress(iter->second.v6_address_, pair.second.port_),
              pair.second.text_);

    pair.second.dirty_ = false;
  }

  for (auto& pair : target_infos_by_name_) {
    pair.second.dirty_ = false;
  }
}

void InstanceSubscriber::Quit() {
  host_->RemoveAgent(service_full_name_);
}

void InstanceSubscriber::ReceivePtrResource(const DnsResource& resource,
                                            MdnsResourceSection section) {
  std::string instance_name;
  if (!MdnsNames::ExtractInstanceName(
          resource.ptr_.pointer_domain_name_.dotted_string_, service_name_,
          &instance_name)) {
    return;
  }

  if (resource.time_to_live_ == 0) {
    if (instance_infos_by_name_.erase(instance_name) != 0) {
      callback_(service_name_, instance_name, SocketAddress::kInvalid,
                SocketAddress::kInvalid, std::vector<std::string>());
    }
  } else if (instance_infos_by_name_.find(instance_name) ==
             instance_infos_by_name_.end()) {
    instance_infos_by_name_.emplace(instance_name, InstanceInfo{});
  }
}

void InstanceSubscriber::ReceiveSrvResource(const DnsResource& resource,
                                            MdnsResourceSection section,
                                            InstanceInfo* instance_info) {
  if (instance_info->target_ != resource.srv_.target_.dotted_string_) {
    instance_info->target_ = resource.srv_.target_.dotted_string_;
    instance_info->dirty_ = true;

    if (target_infos_by_name_.find(instance_info->target_) ==
        target_infos_by_name_.end()) {
      target_infos_by_name_.emplace(instance_info->target_, TargetInfo{});
    }
  }

  if (instance_info->port_ != resource.srv_.port_) {
    instance_info->port_ = resource.srv_.port_;
    instance_info->dirty_ = true;
  }
}

void InstanceSubscriber::ReceiveTxtResource(const DnsResource& resource,
                                            MdnsResourceSection section,
                                            InstanceInfo* instance_info) {
  if (instance_info->text_.size() != resource.txt_.strings_.size()) {
    instance_info->text_.resize(resource.txt_.strings_.size());
    instance_info->dirty_ = true;
  }

  for (size_t i = 0; i < instance_info->text_.size(); ++i) {
    if (instance_info->text_[i] != resource.txt_.strings_[i]) {
      instance_info->text_[i] = resource.txt_.strings_[i];
      instance_info->dirty_ = true;
    }
  }
}

void InstanceSubscriber::ReceiveAResource(const DnsResource& resource,
                                          MdnsResourceSection section,
                                          TargetInfo* target_info) {
  if (target_info->v4_address_ != resource.a_.address_.address_) {
    target_info->v4_address_ = resource.a_.address_.address_;
    target_info->dirty_ = true;
  }
}

void InstanceSubscriber::ReceiveAaaaResource(const DnsResource& resource,
                                             MdnsResourceSection section,
                                             TargetInfo* target_info) {
  if (target_info->v6_address_ != resource.aaaa_.address_.address_) {
    target_info->v6_address_ = resource.aaaa_.address_.address_;
    target_info->dirty_ = true;
  }
}

InstanceSubscriber::InstanceInfo* InstanceSubscriber::FindInstanceInfo(
    const std::string& instance_full_name) {
  std::string instance_name;
  if (!MdnsNames::ExtractInstanceName(instance_full_name, service_name_,
                                      &instance_name)) {
    return nullptr;
  }

  auto iter = instance_infos_by_name_.find(instance_name);
  if (iter == instance_infos_by_name_.end()) {
    return nullptr;
  }

  return &iter->second;
}

}  // namespace mdns
}  // namespace netconnector
