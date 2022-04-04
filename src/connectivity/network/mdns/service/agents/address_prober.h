// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_PROBER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_PROBER_H_

#include <lib/fit/function.h>

#include "src/connectivity/network/mdns/service/agents/prober.h"

namespace mdns {

// Probes for host name conflicts prior to invoking |AddressResponder|.
class AddressProber : public Prober {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates an |AddressProber| for the local host.
  AddressProber(MdnsAgent::Owner* owner, Media media, IpVersions ip_versions,
                CompletionCallback callback);

  // Creates an |AddressProber| for a host identified by |host_full_name| and |addresses|.
  AddressProber(MdnsAgent::Owner* owner, std::string host_full_name,
                std::vector<inet::IpAddress> addresses, Media media, IpVersions ip_versions,
                CompletionCallback callback);

  ~AddressProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;

 private:
  std::string host_full_name_;
  std::vector<inet::IpAddress> addresses_;

 public:
  // Disallow copy, assign and move.
  AddressProber(const AddressProber&) = delete;
  AddressProber(AddressProber&&) = delete;
  AddressProber& operator=(const AddressProber&) = delete;
  AddressProber& operator=(AddressProber&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_AGENTS_ADDRESS_PROBER_H_
