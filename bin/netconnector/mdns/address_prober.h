// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/netconnector/mdns/prober.h"

namespace netconnector {
namespace mdns {

// Probes for host name conflicts prior to invoking |AddressResponder|.
class AddressProber : public Prober {
 public:
  using CompletionCallback = std::function<void(bool)>;

  // Creates an |AddressProber|.
  AddressProber(MdnsAgent::Host* host, const CompletionCallback& callback);

  ~AddressProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;
};

}  // namespace mdns
}  // namespace netconnector
