// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_prober.h"

namespace mdns {

AddressProber::AddressProber(MdnsAgent::Owner* owner, Media media, IpVersions ip_versions,
                             CompletionCallback callback)
    : Prober(owner, DnsType::kA, media, ip_versions, std::move(callback)) {}

AddressProber::AddressProber(MdnsAgent::Owner* owner, std::string host_full_name,
                             std::vector<inet::IpAddress> addresses, Media media,
                             IpVersions ip_versions, CompletionCallback callback)
    : Prober(owner, DnsType::kA, media, ip_versions, std::move(callback)),
      host_full_name_(std::move(host_full_name)),
      addresses_(std::move(addresses)) {
  FX_DCHECK(!host_full_name_.empty());
  FX_DCHECK(!addresses_.empty());
}

AddressProber::~AddressProber() {}

const std::string& AddressProber::ResourceName() {
  return host_full_name_.empty() ? local_host_full_name() : host_full_name_;
}

void AddressProber::SendProposedResources(MdnsResourceSection section) {
  if (addresses_.empty()) {
    // Send addresses for the local host.
    SendAddresses(section, ReplyAddress::Multicast(media(), ip_versions()));
  } else {
    // Send addresses that were provided in the constructor.
    for (const auto& address : addresses_) {
      SendResource(std::make_shared<DnsResource>(host_full_name_, address), section,
                   ReplyAddress::Multicast(media(), ip_versions()));
    }
  }
}

}  // namespace mdns
