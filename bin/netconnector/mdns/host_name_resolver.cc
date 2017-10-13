// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/host_name_resolver.h"

#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

HostNameResolver::HostNameResolver(
    MdnsAgent::Host* host,
    const std::string& host_name,
    fxl::TimePoint timeout,
    const Mdns::ResolveHostNameCallback& callback)
    : host_(host),
      host_name_(host_name),
      host_full_name_(MdnsNames::LocalHostFullName(host_name)),
      timeout_(timeout),
      callback_(callback) {
  FXL_DCHECK(callback_);
}

HostNameResolver::~HostNameResolver() {}

void HostNameResolver::Start() {
  host_->SendQuestion(
      std::make_shared<DnsQuestion>(host_full_name_, DnsType::kA),
      fxl::TimePoint::Now());
  host_->SendQuestion(
      std::make_shared<DnsQuestion>(host_full_name_, DnsType::kAaaa),
      fxl::TimePoint::Now());

  host_->WakeAt(shared_from_this(), timeout_);
}

void HostNameResolver::Wake() {
  if (callback_) {
    callback_(host_name_, v4_address_, v6_address_);
    callback_ = nullptr;
    host_->RemoveAgent(this);
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
  FXL_DCHECK(callback_);

  if (v4_address_ || v6_address_) {
    callback_(host_name_, v4_address_, v6_address_);
    callback_ = nullptr;
    host_->RemoveAgent(this);
  }
}

void HostNameResolver::Quit() {
  FXL_DCHECK(callback_);
  callback_(host_name_, v4_address_, v6_address_);
  callback_ = nullptr;

  host_->RemoveAgent(this);
}

}  // namespace mdns
}  // namespace netconnector
