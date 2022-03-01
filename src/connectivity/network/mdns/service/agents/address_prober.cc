// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_prober.h"

namespace mdns {

AddressProber::AddressProber(MdnsAgent::Owner* owner, CompletionCallback callback)
    : Prober(owner, DnsType::kA, std::move(callback)) {}

AddressProber::~AddressProber() {}

const std::string& AddressProber::ResourceName() { return local_host_full_name(); }

void AddressProber::SendProposedResources(MdnsResourceSection section) {
  SendAddresses(section, ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
}

}  // namespace mdns
