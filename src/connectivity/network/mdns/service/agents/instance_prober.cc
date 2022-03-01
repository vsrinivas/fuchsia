// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/instance_prober.h"

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

InstanceProber::InstanceProber(MdnsAgent::Owner* owner, const std::string& service_name,
                               const std::string& instance_name, inet::IpPort port,
                               CompletionCallback callback)
    : Prober(owner, DnsType::kSrv, std::move(callback)),
      instance_full_name_(MdnsNames::InstanceFullName(instance_name, service_name)),
      port_(port) {}

InstanceProber::~InstanceProber() {}

const std::string& InstanceProber::ResourceName() { return instance_full_name_; }

void InstanceProber::SendProposedResources(MdnsResourceSection section) {
  auto srv_resource = std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv);
  srv_resource->srv_.port_ = port_;
  srv_resource->srv_.target_ = local_host_full_name();
  SendResource(srv_resource, section, ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
}

}  // namespace mdns
