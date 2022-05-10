// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/host_name_requestor.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <algorithm>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

HostNameRequestor::HostNameRequestor(MdnsAgent::Owner* owner, const std::string& host_name,
                                     Media media, IpVersions ip_versions)
    : MdnsAgent(owner),
      host_name_(host_name),
      host_full_name_(MdnsNames::HostFullName(host_name)),
      media_(media),
      ip_versions_(ip_versions) {}

HostNameRequestor::~HostNameRequestor() {}

void HostNameRequestor::AddSubscriber(Mdns::HostNameSubscriber* subscriber) {
  subscribers_.insert(subscriber);
  if (started() && !prev_addresses_.empty()) {
    subscriber->AddressesChanged(prev_addresses());
  }
}

void HostNameRequestor::RemoveSubscriber(Mdns::HostNameSubscriber* subscriber) {
  subscribers_.erase(subscriber);
  if (subscribers_.empty()) {
    RemoveSelf();
  }
}

void HostNameRequestor::Start(const std::string& local_host_full_name) {
  // Note that |host_full_name_| is the name we're trying to resolve, not the
  // name of the local host, which is the parameter to this method.

  MdnsAgent::Start(local_host_full_name);

  SendQuestion(std::make_shared<DnsQuestion>(host_full_name_, DnsType::kA),
               ReplyAddress::Multicast(media_, ip_versions_));
  SendQuestion(std::make_shared<DnsQuestion>(host_full_name_, DnsType::kAaaa),
               ReplyAddress::Multicast(media_, ip_versions_));
}

void HostNameRequestor::ReceiveResource(const DnsResource& resource, MdnsResourceSection section,
                                        ReplyAddress sender_address) {
  if (!sender_address.Matches(media_) || !sender_address.Matches(ip_versions_) ||
      resource.name_.dotted_string_ != host_full_name_) {
    return;
  }

  HostAddress address;
  if (resource.type_ == DnsType::kA) {
    address = HostAddress(resource.a_.address_.address_, sender_address.interface_id(),
                          zx::sec(resource.time_to_live_));
  } else if (resource.type_ == DnsType::kAaaa) {
    address = HostAddress(resource.aaaa_.address_.address_, sender_address.interface_id(),
                          zx::sec(resource.time_to_live_));
  }

  if (section == MdnsResourceSection::kExpired) {
    if (prev_addresses_.count(address) == 0) {
      return;
    }

    if (addresses_.empty()) {
      addresses_ = prev_addresses_;
    }

    addresses_.erase(address);

    if (addresses_.empty()) {
      SendAddressesChanged();
    }

    return;
  }

  // Add an address.
  addresses_.insert(std::move(address));

  Renew(resource, media_, ip_versions_);
}

void HostNameRequestor::EndOfMessage() {
  if (addresses_.empty()) {
    return;
  }

  if (addresses_ == prev_addresses_) {
    addresses_.clear();
    return;
  }

  SendAddressesChanged();
}

void HostNameRequestor::SendAddressesChanged() {
  for (auto subscriber : subscribers_) {
    subscriber->AddressesChanged(addresses());
  }

  prev_addresses_ = std::move(addresses_);
}

}  // namespace mdns
