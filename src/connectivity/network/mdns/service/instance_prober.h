// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_PROBER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_PROBER_H_

#include <lib/fit/function.h>

#include "src/connectivity/network/mdns/service/prober.h"

namespace mdns {

// Probes for SRV record conflicts prior to invoking |InstanceResponder|.
class InstanceProber : public Prober {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates a |InstanceProber|.
  InstanceProber(MdnsAgent::Host* host, const std::string& service_name,
                 const std::string& instance_name, inet::IpPort port, CompletionCallback callback);

  ~InstanceProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;

 private:
  std::string instance_full_name_;
  inet::IpPort port_;

 public:
  // Disallow copy, assign and move.
  InstanceProber(const InstanceProber&) = delete;
  InstanceProber(InstanceProber&&) = delete;
  InstanceProber& operator=(const InstanceProber&) = delete;
  InstanceProber& operator=(InstanceProber&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_INSTANCE_PROBER_H_
