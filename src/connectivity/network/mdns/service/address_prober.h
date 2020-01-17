// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_PROBER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_PROBER_H_

#include <lib/fit/function.h>

#include "src/connectivity/network/mdns/service/prober.h"

namespace mdns {

// Probes for host name conflicts prior to invoking |AddressResponder|.
class AddressProber : public Prober {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates an |AddressProber|.
  AddressProber(MdnsAgent::Host* host, CompletionCallback callback);

  ~AddressProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;

 public:
  // Disallow copy, assign and move.
  AddressProber(const AddressProber&) = delete;
  AddressProber(AddressProber&&) = delete;
  AddressProber& operator=(const AddressProber&) = delete;
  AddressProber& operator=(AddressProber&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_ADDRESS_PROBER_H_
