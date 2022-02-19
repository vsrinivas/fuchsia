// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/service_instance_resolver.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

ServiceInstanceResolver::ServiceInstanceResolver(MdnsAgent::Host* host, const std::string& service,
                                                 const std::string& instance, zx::time timeout,
                                                 Mdns::ResolveServiceInstanceCallback callback)
    : MdnsAgent(host),
      service_(service),
      instance_name_(instance),
      timeout_(timeout),
      callback_(std::move(callback)) {
  FX_DCHECK(callback_);
}

ServiceInstanceResolver::~ServiceInstanceResolver() {}

void ServiceInstanceResolver::Quit() {
  if (callback_) {
    callback_(std::move(instance_));
    callback_ = nullptr;
  }

  MdnsAgent::Quit();
}

void ServiceInstanceResolver::EndOfMessage() {
  if (!callback_) {
    // This can happen when a redundant response is received after the block below runs and before
    // the posted task runs, e.g. when two NICs are connected to the same LAN.
    return;
  }

  if (port_.is_valid() && (instance_.has_ipv4_endpoint() || instance_.has_ipv6_endpoint())) {
    callback_(std::move(instance_));
    callback_ = nullptr;
    PostTaskForTime([this]() { RemoveSelf(); }, now());
  }
}

void ServiceInstanceResolver::Start(const std::string& service_instance,
                                    const MdnsAddresses& addresses) {
  MdnsAgent::Start(service_instance, addresses);
  service_instance_ = MdnsNames::LocalInstanceFullName(instance_name_, service_);
  SendQuestion(std::make_shared<DnsQuestion>(service_instance_, DnsType::kSrv));

  PostTaskForTime(
      [this]() {
        if (callback_) {
          callback_(std::move(instance_));
          callback_ = nullptr;
          RemoveSelf();
        }
      },
      timeout_);
}

void ServiceInstanceResolver::ReceiveResource(const DnsResource& resource,
                                              MdnsResourceSection section) {
  switch (resource.type_) {
    case DnsType::kSrv:
      if (resource.name_.dotted_string_ == service_instance_) {
        instance_.set_service(service_);
        instance_.set_instance(instance_name_);
        instance_.set_srv_priority(resource.srv_.priority_);
        instance_.set_srv_weight(resource.srv_.weight_);
        port_ = resource.srv_.port_;
        instance_.set_target(resource.srv_.target_.dotted_string_);
      }
      break;
    case DnsType::kA:
      if (instance_.has_target() && (resource.name_.dotted_string_ == instance_.target())) {
        instance_.set_ipv4_endpoint(MdnsFidlUtil::CreateSocketAddressV4(
            inet::SocketAddress(resource.a_.address_.address_, port_)));
      }
      break;
    case DnsType::kAaaa:
      if (instance_.has_target() && (resource.name_.dotted_string_ == instance_.target())) {
        instance_.set_ipv6_endpoint(MdnsFidlUtil::CreateSocketAddressV6(
            inet::SocketAddress(resource.aaaa_.address_.address_, port_)));
      }
      break;
    case DnsType::kTxt:
      if (instance_.has_target() && (resource.name_.dotted_string_ == instance_.target())) {
        instance_.set_text(resource.txt_.strings_);
      }
      break;
    default:
      break;
  }
}

}  // namespace mdns
