// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/host_name_resolver.h"

#include "apps/netconnector/src/mdns/mdns_names.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

HostNameResolver::HostNameResolver(
    MdnsAgent::Host* host,
    const std::string& host_name,
    const std::string& host_full_name,
    ftl::TimePoint timeout,
    const Mdns::ResolveHostNameCallback& callback)
    : host_(host),
      host_name_(host_name),
      host_full_name_(host_full_name),
      timeout_(timeout),
      callback_(callback) {
  FTL_DCHECK(callback_);
}

HostNameResolver::~HostNameResolver() {}

void HostNameResolver::Start() {
  host_->SendQuestion(
      std::make_shared<DnsQuestion>(host_full_name_, DnsType::kA),
      ftl::TimePoint::Now());
  host_->SendQuestion(
      std::make_shared<DnsQuestion>(host_full_name_, DnsType::kAaaa),
      ftl::TimePoint::Now());

  host_->WakeAt(shared_from_this(), timeout_);
}

void HostNameResolver::Wake() {
  if (callback_) {
    callback_(host_name_, v4_address_, v6_address_);
    callback_ = nullptr;
    host_->RemoveAgent(host_full_name_);
  }
}

void HostNameResolver::ReceiveQuestion(const DnsQuestion& question) {}

void HostNameResolver::ReceiveResource(const DnsResource& resource,
                                       MdnsResourceSection section) {
  if (resource.name_.dotted_string_ != host_full_name_) {
    return;
  }

  if (resource.type_ == DnsType::kA) {
    v4_address_ = resource.a_.address_.address_;
  } else if (resource.type_ == DnsType::kAaaa) {
    v6_address_ = resource.aaaa_.address_.address_;
  }
}

void HostNameResolver::EndOfMessage() {
  FTL_DCHECK(callback_);

  if (v4_address_ || v6_address_) {
    callback_(host_name_, v4_address_, v6_address_);
    callback_ = nullptr;
    host_->RemoveAgent(host_full_name_);
  }
}

void HostNameResolver::Quit() {
  FTL_DCHECK(callback_);
  callback_(host_name_, v4_address_, v6_address_);
  callback_ = nullptr;

  host_->RemoveAgent(host_full_name_);
}

}  // namespace mdns
}  // namespace netconnector
