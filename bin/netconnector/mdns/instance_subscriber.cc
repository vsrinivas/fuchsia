// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/instance_subscriber.h"

#include "apps/netconnector/src/mdns/mdns_names.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {
namespace {

// static
static constexpr ftl::TimeDelta kMaxQueryInterval =
    ftl::TimeDelta::FromSeconds(60 * 60);

}  // namespace

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

  if (query_delay_ == ftl::TimeDelta::Zero()) {
    query_delay_ = ftl::TimeDelta::FromSeconds(1);
  } else {
    query_delay_ = query_delay_ * 2;
    if (query_delay_ > kMaxQueryInterval) {
      query_delay_ = kMaxQueryInterval;
    }
  }

  host_->WakeAt(shared_from_this(), ftl::TimePoint::Now() + query_delay_);
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
      auto iter =
          instance_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != instance_infos_by_full_name_.end()) {
        ReceiveSrvResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kTxt: {
      auto iter =
          instance_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != instance_infos_by_full_name_.end()) {
        ReceiveTxtResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kA: {
      auto iter =
          target_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_full_name_.end()) {
        ReceiveAResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kAaaa: {
      auto iter =
          target_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_full_name_.end()) {
        ReceiveAaaaResource(resource, section, &iter->second);
      }
    } break;
    default:
      break;
  }
}

void InstanceSubscriber::EndOfMessage() {
  // Use the callback to report updates.
  for (auto& pair : instance_infos_by_full_name_) {
    InstanceInfo& instance_info = pair.second;

    if (instance_info.target_.empty()) {
      // We haven't yet seen an SRV record for this instance.
      continue;
    }

    auto iter = target_infos_by_full_name_.find(instance_info.target_);
    FTL_DCHECK(iter != target_infos_by_full_name_.end());
    TargetInfo& target_info = iter->second;

    // Keep this target info around.
    target_info.keep_ = true;

    if (!instance_info.dirty_ && !target_info.dirty_) {
      // Both the instance info and target info are clean.
      continue;
    }

    if (!target_info.v4_address_ && !target_info.v6_address_) {
      // No addresses yet.
      continue;
    }

    // Something has changed.
    callback_(service_name_, instance_info.instance_name_,
              SocketAddress(target_info.v4_address_, instance_info.port_),
              SocketAddress(target_info.v6_address_, instance_info.port_),
              instance_info.text_);

    instance_info.dirty_ = false;
  }

  // Clean up |target_infos_by_full_name_|.
  for (auto iter = target_infos_by_full_name_.begin();
       iter != target_infos_by_full_name_.end();) {
    if (iter->second.keep_) {
      iter->second.dirty_ = false;
      iter->second.keep_ = false;
      ++iter;
    } else {
      // No instances reference this target. Get rid of it.
      iter = target_infos_by_full_name_.erase(iter);
    }
  }
}

void InstanceSubscriber::Quit() {
  host_->RemoveAgent(service_full_name_);
}

void InstanceSubscriber::ReceivePtrResource(const DnsResource& resource,
                                            MdnsResourceSection section) {
  const std::string& instance_full_name =
      resource.ptr_.pointer_domain_name_.dotted_string_;

  std::string instance_name;
  if (!MdnsNames::ExtractInstanceName(instance_full_name, service_name_,
                                      &instance_name)) {
    return;
  }

  if (resource.time_to_live_ == 0) {
    RemoveInstance(instance_full_name);
    return;
  }

  if (instance_infos_by_full_name_.find(instance_full_name) ==
      instance_infos_by_full_name_.end()) {
    auto pair = instance_infos_by_full_name_.emplace(instance_full_name,
                                                     InstanceInfo{});
    FTL_DCHECK(pair.second);
    pair.first->second.instance_name_ = instance_name;
  }

  host_->Renew(resource);
}

void InstanceSubscriber::ReceiveSrvResource(const DnsResource& resource,
                                            MdnsResourceSection section,
                                            InstanceInfo* instance_info) {
  if (resource.time_to_live_ == 0) {
    RemoveInstance(resource.name_.dotted_string_);
    return;
  }

  if (instance_info->target_ != resource.srv_.target_.dotted_string_) {
    instance_info->target_ = resource.srv_.target_.dotted_string_;
    instance_info->dirty_ = true;

    if (target_infos_by_full_name_.find(instance_info->target_) ==
        target_infos_by_full_name_.end()) {
      target_infos_by_full_name_.emplace(instance_info->target_, TargetInfo{});
    }
  }

  if (instance_info->port_ != resource.srv_.port_) {
    instance_info->port_ = resource.srv_.port_;
    instance_info->dirty_ = true;
  }

  host_->Renew(resource);
}

void InstanceSubscriber::ReceiveTxtResource(const DnsResource& resource,
                                            MdnsResourceSection section,
                                            InstanceInfo* instance_info) {
  if (resource.time_to_live_ == 0) {
    if (!instance_info->text_.empty()) {
      instance_info->text_.clear();
      instance_info->dirty_ = true;
    }

    return;
  }

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

  host_->Renew(resource);
}

void InstanceSubscriber::ReceiveAResource(const DnsResource& resource,
                                          MdnsResourceSection section,
                                          TargetInfo* target_info) {
  if (resource.time_to_live_ == 0) {
    if (target_info->v4_address_) {
      target_info->v4_address_ = IpAddress::kInvalid;
      target_info->dirty_ = true;
    }

    return;
  }

  if (target_info->v4_address_ != resource.a_.address_.address_) {
    target_info->v4_address_ = resource.a_.address_.address_;
    target_info->dirty_ = true;
  }

  host_->Renew(resource);
}

void InstanceSubscriber::ReceiveAaaaResource(const DnsResource& resource,
                                             MdnsResourceSection section,
                                             TargetInfo* target_info) {
  if (resource.time_to_live_ == 0) {
    if (target_info->v6_address_) {
      target_info->v6_address_ = IpAddress::kInvalid;
      target_info->dirty_ = true;
    }

    return;
  }

  if (target_info->v6_address_ != resource.aaaa_.address_.address_) {
    target_info->v6_address_ = resource.aaaa_.address_.address_;
    target_info->dirty_ = true;
  }

  host_->Renew(resource);
}

void InstanceSubscriber::RemoveInstance(const std::string& instance_full_name) {
  auto iter = instance_infos_by_full_name_.find(instance_full_name);
  if (iter != instance_infos_by_full_name_.end()) {
    callback_(service_name_, iter->second.instance_name_,
              SocketAddress::kInvalid, SocketAddress::kInvalid,
              std::vector<std::string>());
    instance_infos_by_full_name_.erase(iter);
  }
}

}  // namespace mdns
}  // namespace netconnector
