// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/address_prober.h"

#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

AddressProber::AddressProber(MdnsAgent::Host* host,
                             const CompletionCallback& callback)
    : Prober(host, DnsType::kA, callback) {}

AddressProber::~AddressProber() {}

const std::string& AddressProber::ResourceName() {
  return host_full_name();
}

void AddressProber::SendProposedResources(MdnsResourceSection section) {
  SendAddresses(section);
}

}  // namespace mdns
}  // namespace netconnector
