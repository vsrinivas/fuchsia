// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/address_responder.h"

#include "apps/netconnector/src/mdns/mdns_names.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace netconnector {
namespace mdns {

// static
const std::string AddressResponder::kName = "##address responder##";

AddressResponder::AddressResponder(MdnsAgent::Host* host,
                                   const std::string& host_full_name)
    : host_(host), host_full_name_(host_full_name) {}

AddressResponder::~AddressResponder() {}

void AddressResponder::Start() {}

void AddressResponder::Wake() {}

void AddressResponder::ReceiveQuestion(const DnsQuestion& question) {
  if ((question.type_ == DnsType::kA || question.type_ == DnsType::kAaaa) &&
      question.name_.dotted_string_ == host_full_name_) {
    host_->SendAddresses(MdnsResourceSection::kAnswer, ftl::TimePoint::Now());
  }
}

void AddressResponder::ReceiveResource(const DnsResource& resource,
                                       MdnsResourceSection section) {}

void AddressResponder::EndOfMessage() {}

void AddressResponder::Quit() {
  host_->RemoveAgent(kName);
}

}  // namespace mdns
}  // namespace netconnector
