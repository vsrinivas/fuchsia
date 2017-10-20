// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/address_responder.h"

#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace netconnector {
namespace mdns {

AddressResponder::AddressResponder(MdnsAgent::Host* host,
                                   const std::string& host_full_name)
    : MdnsAgent(host), host_full_name_(host_full_name) {}

AddressResponder::~AddressResponder() {}

void AddressResponder::ReceiveQuestion(const DnsQuestion& question) {
  if ((question.type_ == DnsType::kA || question.type_ == DnsType::kAaaa ||
       question.type_ == DnsType::kAny) &&
      question.name_.dotted_string_ == host_full_name_) {
    SendAddresses(MdnsResourceSection::kAnswer);
  }
}

}  // namespace mdns
}  // namespace netconnector
