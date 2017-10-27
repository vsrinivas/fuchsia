// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/netconnector/mdns/prober.h"

namespace netconnector {
namespace mdns {

// Probes for SRV record conflicts prior to invoking |InstanceResponder|.
class InstanceProber : public Prober {
 public:
  using CompletionCallback = std::function<void(bool)>;

  // Creates a |InstanceProber|.
  InstanceProber(MdnsAgent::Host* host,
                 const std::string& service_name,
                 const std::string& instance_name,
                 IpPort port,
                 const CompletionCallback& callback);

  ~InstanceProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;

 private:
  std::string instance_full_name_;
  IpPort port_;
};

}  // namespace mdns
}  // namespace netconnector
