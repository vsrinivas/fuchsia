// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/address_prober.h"

namespace mdns {

AddressProber::AddressProber(MdnsAgent::Host* host, CompletionCallback callback)
    : Prober(host, DnsType::kA, std::move(callback)) {}

AddressProber::~AddressProber() {}

const std::string& AddressProber::ResourceName() { return host_full_name(); }

void AddressProber::SendProposedResources(MdnsResourceSection section) {
  SendAddresses(section, addresses().multicast_reply());
}

}  // namespace mdns
