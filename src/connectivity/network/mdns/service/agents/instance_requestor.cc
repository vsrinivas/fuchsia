// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/instance_requestor.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <iterator>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/types.h"

namespace mdns {
namespace {

// static
static constexpr zx::duration kMaxQueryInterval = zx::hour(1);

}  // namespace

InstanceRequestor::InstanceRequestor(MdnsAgent::Owner* owner, const std::string& service_name,
                                     Media media, IpVersions ip_versions, bool include_local,
                                     bool include_local_proxies)
    : MdnsAgent(owner),
      service_name_(service_name),
      service_full_name_(MdnsNames::ServiceFullName(service_name)),
      media_(media),
      ip_versions_(ip_versions),
      include_local_(include_local),
      include_local_proxies_(include_local_proxies),
      question_(std::make_shared<DnsQuestion>(service_full_name_, DnsType::kPtr)) {}

void InstanceRequestor::AddSubscriber(Mdns::Subscriber* subscriber) {
  subscribers_.insert(subscriber);
  if (started()) {
    ReportAllDiscoveries(subscriber);
  }
}

void InstanceRequestor::RemoveSubscriber(Mdns::Subscriber* subscriber) {
  subscribers_.erase(subscriber);
  if (subscribers_.empty()) {
    PostTaskForTime([this, own_this = shared_from_this()]() { Quit(); }, now());
  }
}

void InstanceRequestor::Start(const std::string& local_host_full_name) {
  MdnsAgent::Start(local_host_full_name);
  SendQuery();
}

void InstanceRequestor::ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                                        ReplyAddress sender_address) {
  if (!sender_address.Matches(media_) || !sender_address.Matches(ip_versions_)) {
    return;
  }

  switch (resource.type_) {
    case DnsType::kPtr:
      if (resource.name_.dotted_string_ == service_full_name_) {
        ReceivePtrResource(resource, section);
      }
      break;
    case DnsType::kSrv: {
      auto iter = instance_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != instance_infos_by_full_name_.end()) {
        ReceiveSrvResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kTxt: {
      auto iter = instance_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != instance_infos_by_full_name_.end()) {
        ReceiveTxtResource(resource, section, &iter->second);
      }
    } break;
    case DnsType::kA: {
      auto iter = target_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_full_name_.end()) {
        ReceiveAResource(resource, section, &iter->second, sender_address.interface_id());
      }
    } break;
    case DnsType::kAaaa: {
      auto iter = target_infos_by_full_name_.find(resource.name_.dotted_string_);
      if (iter != target_infos_by_full_name_.end()) {
        ReceiveAaaaResource(resource, section, &iter->second, sender_address.interface_id());
      }
    } break;
    default:
      break;
  }
}

void InstanceRequestor::EndOfMessage() {
  // Report updates.
  for (auto& [_, instance_info] : instance_infos_by_full_name_) {
    if (instance_info.target_.empty()) {
      // We haven't yet seen an SRV record for this instance.
      continue;
    }

    auto iter = target_infos_by_full_name_.find(instance_info.target_full_name_);
    FX_DCHECK(iter != target_infos_by_full_name_.end());
    TargetInfo& target_info = iter->second;

    // Keep this target info around.
    target_info.keep_ = true;

    if (!instance_info.dirty_ && !target_info.dirty_) {
      // Both the instance info and target info are clean.
      continue;
    }

    if (target_info.addresses_.empty()) {
      // No addresses yet.
      continue;
    }

    // Something has changed.
    std::vector<Mdns::Subscriber*> subscribers(subscribers_.begin(), subscribers_.end());
    if (instance_info.new_) {
      instance_info.new_ = false;
      for (auto subscriber : subscribers) {
        subscriber->InstanceDiscovered(service_name_, instance_info.instance_name_,
                                       Addresses(target_info, instance_info.port_),
                                       instance_info.text_, instance_info.srv_priority_,
                                       instance_info.srv_weight_, instance_info.target_);
      }
    } else {
      for (auto subscriber : subscribers) {
        subscriber->InstanceChanged(service_name_, instance_info.instance_name_,
                                    Addresses(target_info, instance_info.port_),
                                    instance_info.text_, instance_info.srv_priority_,
                                    instance_info.srv_weight_, instance_info.target_);
      }
    }

    instance_info.dirty_ = false;
  }

  // Clean up |target_infos_by_full_name_|.
  for (auto iter = target_infos_by_full_name_.begin(); iter != target_infos_by_full_name_.end();) {
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

void InstanceRequestor::ReportAllDiscoveries(Mdns::Subscriber* subscriber) {
  for (const auto& [_, instance_info] : instance_infos_by_full_name_) {
    if (instance_info.target_.empty()) {
      // We haven't yet seen an SRV record for this instance.
      continue;
    }

    auto iter = target_infos_by_full_name_.find(instance_info.target_full_name_);
    FX_DCHECK(iter != target_infos_by_full_name_.end());
    TargetInfo& target_info = iter->second;

    if (target_info.addresses_.empty()) {
      // No addresses yet.
      continue;
    }

    subscriber->InstanceDiscovered(service_name_, instance_info.instance_name_,
                                   Addresses(target_info, instance_info.port_), instance_info.text_,
                                   instance_info.srv_priority_, instance_info.srv_weight_,
                                   instance_info.target_);
  }
}

void InstanceRequestor::SendQuery() {
  SendQuestion(question_, ReplyAddress::Multicast(media_, ip_versions_));
  for (const auto& subscriber : subscribers_) {
    subscriber->Query(question_->type_);
  }

  if (query_delay_ == zx::sec(0)) {
    query_delay_ = zx::sec(1);
  } else {
    query_delay_ = query_delay_ * 2;
    if (query_delay_ > kMaxQueryInterval) {
      query_delay_ = kMaxQueryInterval;
    }
  }

  PostTaskForTime([this]() { SendQuery(); }, now() + query_delay_);
}

void InstanceRequestor::ReceivePtrResource(const DnsResource& resource,
                                           MdnsResourceSection section) {
  const std::string& instance_full_name = resource.ptr_.pointer_domain_name_.dotted_string_;

  std::string instance_name;
  if (!MdnsNames::ExtractInstanceName(instance_full_name, service_name_, &instance_name)) {
    return;
  }

  if (resource.time_to_live_ == 0) {
    RemoveInstance(instance_full_name);
    return;
  }

  if (instance_infos_by_full_name_.find(instance_full_name) == instance_infos_by_full_name_.end()) {
    auto [iter, inserted] =
        instance_infos_by_full_name_.emplace(instance_full_name, InstanceInfo{});
    FX_DCHECK(inserted);
    iter->second.instance_name_ = instance_name;
  }

  Renew(resource, media_, ip_versions_);
}

void InstanceRequestor::ReceiveSrvResource(const DnsResource& resource, MdnsResourceSection section,
                                           InstanceInfo* instance_info) {
  if (resource.time_to_live_ == 0) {
    RemoveInstance(resource.name_.dotted_string_);
    return;
  }

  if (instance_info->target_.empty() ||
      MdnsNames::HostFullName(instance_info->target_) != resource.srv_.target_.dotted_string_) {
    instance_info->target_ = MdnsNames::HostNameFromFullName(resource.srv_.target_.dotted_string_);
    instance_info->target_full_name_ = resource.srv_.target_.dotted_string_;
    instance_info->dirty_ = true;

    if (target_infos_by_full_name_.find(instance_info->target_full_name_) ==
        target_infos_by_full_name_.end()) {
      target_infos_by_full_name_.emplace(instance_info->target_full_name_, TargetInfo{});
    }
  }

  if (instance_info->srv_priority_ != resource.srv_.priority_) {
    instance_info->srv_priority_ = resource.srv_.priority_;
    instance_info->dirty_ = true;
  }

  if (instance_info->srv_weight_ != resource.srv_.weight_) {
    instance_info->srv_weight_ = resource.srv_.weight_;
    instance_info->dirty_ = true;
  }

  if (instance_info->port_ != resource.srv_.port_) {
    instance_info->port_ = resource.srv_.port_;
    instance_info->dirty_ = true;
  }

  Renew(resource, media_, ip_versions_);
}

void InstanceRequestor::ReceiveTxtResource(const DnsResource& resource, MdnsResourceSection section,
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

  Renew(resource, media_, ip_versions_);
}

void InstanceRequestor::ReceiveAResource(const DnsResource& resource, MdnsResourceSection section,
                                         TargetInfo* target_info, uint32_t scope_id) {
  inet::SocketAddress address(resource.a_.address_.address_, inet::IpPort(), scope_id);

  if (resource.time_to_live_ == 0) {
    if (target_info->addresses_.erase(address)) {
      target_info->dirty_ = true;
    }

    return;
  }

  if (target_info->addresses_.insert(address).second) {
    target_info->dirty_ = true;
  }

  Renew(resource, media_, ip_versions_);
}

void InstanceRequestor::ReceiveAaaaResource(const DnsResource& resource,
                                            MdnsResourceSection section, TargetInfo* target_info,
                                            uint32_t scope_id) {
  inet::SocketAddress address(resource.aaaa_.address_.address_, inet::IpPort(), scope_id);

  if (resource.time_to_live_ == 0) {
    if (target_info->addresses_.erase(address)) {
      target_info->dirty_ = true;
    }

    return;
  }

  if (target_info->addresses_.insert(address).second) {
    target_info->dirty_ = true;
  }

  Renew(resource, media_, ip_versions_);
}

void InstanceRequestor::RemoveInstance(const std::string& instance_full_name) {
  auto iter = instance_infos_by_full_name_.find(instance_full_name);
  if (iter != instance_infos_by_full_name_.end()) {
    for (auto subscriber : subscribers_) {
      subscriber->InstanceLost(service_name_, iter->second.instance_name_);
    }

    instance_infos_by_full_name_.erase(iter);
  }
}

std::vector<inet::SocketAddress> InstanceRequestor::Addresses(const TargetInfo& target_info,
                                                              inet::IpPort port) {
  std::vector<inet::SocketAddress> result;
  std::transform(target_info.addresses_.begin(), target_info.addresses_.end(),
                 std::back_inserter(result), [port](const inet::SocketAddress& address) {
                   return inet::SocketAddress(address.address(), port, address.scope_id());
                 });
  return result;
}

void InstanceRequestor::OnAddLocalServiceInstance(const Mdns::ServiceInstance& instance,
                                                  bool from_proxy) {
  if (from_proxy ? !include_local_proxies_ : !include_local_) {
    return;
  }

  if (instance.service_name_ != service_name_) {
    return;
  }

  FX_DCHECK(!instance.addresses_.empty());

  auto instance_full_name = MdnsNames::InstanceFullName(instance.instance_name_, service_name_);
  auto [instance_iter, inserted] =
      instance_infos_by_full_name_.insert(std::make_pair(instance_full_name, InstanceInfo()));
  auto& instance_info = instance_iter->second;
  instance_info.instance_name_ = instance.instance_name_;
  instance_info.target_ = instance.target_name_;
  instance_info.target_full_name_ = MdnsNames::HostFullName(instance.target_name_);
  instance_info.port_ = instance.addresses_[0].port();
  instance_info.text_ = instance.text_;
  instance_info.srv_priority_ = instance.srv_priority_;
  instance_info.srv_weight_ = instance.srv_weight_;
  instance_info.new_ = inserted;

  auto [target_iter, _] = target_infos_by_full_name_.insert(
      std::make_pair(instance_info.target_full_name_, TargetInfo()));
  auto& target_info = target_iter->second;

  std::copy(instance.addresses_.begin(), instance.addresses_.end(),
            std::inserter(target_info.addresses_, target_info.addresses_.end()));
  target_info.dirty_ = true;

  EndOfMessage();
}

void InstanceRequestor::OnChangeLocalServiceInstance(const Mdns::ServiceInstance& instance,
                                                     bool from_proxy) {
  if (from_proxy ? !include_local_proxies_ : !include_local_) {
    return;
  }

  if (instance.service_name_ != service_name_) {
    return;
  }

  FX_DCHECK(!instance.addresses_.empty());

  auto instance_full_name =
      MdnsNames::InstanceFullName(instance.instance_name_, instance.service_name_);
  auto iter = instance_infos_by_full_name_.find(instance_full_name);
  if (iter == instance_infos_by_full_name_.end()) {
    return;
  }

  auto& instance_info = iter->second;
  FX_DCHECK(instance_info.instance_name_ == instance.instance_name_);
  if (instance_info.target_ != instance.target_name_) {
    instance_info.target_ = instance.target_name_;
    instance_info.target_full_name_ = MdnsNames::HostFullName(instance.target_name_);
    instance_info.dirty_ = true;
  }

  if (instance_info.port_ != instance.addresses_[0].port()) {
    instance_info.port_ = instance.addresses_[0].port();
    instance_info.dirty_ = true;
  }

  if (instance_info.text_ != instance.text_) {
    instance_info.text_ = instance.text_;
    instance_info.dirty_ = true;
  }

  if (instance_info.srv_priority_ != instance.srv_priority_) {
    instance_info.srv_priority_ = instance.srv_priority_;
    instance_info.dirty_ = true;
  }

  if (instance_info.srv_weight_ != instance.srv_weight_) {
    instance_info.srv_weight_ = instance.srv_weight_;
    instance_info.dirty_ = true;
  }

  auto target_full_name = MdnsNames::HostFullName(instance.target_name_);
  auto [target_iter, _] =
      target_infos_by_full_name_.insert(std::make_pair(target_full_name, TargetInfo()));
  auto& target_info = target_iter->second;

  std::unordered_set<inet::SocketAddress> addresses;
  std::copy(instance.addresses_.begin(), instance.addresses_.end(),
            std::inserter(addresses, addresses.end()));
  if (target_info.addresses_ != addresses) {
    target_info.addresses_ = addresses;
    target_info.dirty_ = true;
  }

  if (instance_info.dirty_ || target_info.dirty_) {
    EndOfMessage();
  }
}

void InstanceRequestor::OnRemoveLocalServiceInstance(const std::string& service_name,
                                                     const std::string& instance_name,
                                                     bool from_proxy) {
  if (from_proxy ? !include_local_proxies_ : !include_local_) {
    return;
  }

  if (service_name_ != service_name_) {
    return;
  }

  RemoveInstance(MdnsNames::InstanceFullName(instance_name, service_name_));

  EndOfMessage();
}

}  // namespace mdns
