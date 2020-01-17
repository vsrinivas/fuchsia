// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/address_responder.h"

#include "src/connectivity/network/mdns/service/mdns_names.h"
#include "src/lib/syslog/cpp/logger.h"

namespace mdns {

AddressResponder::AddressResponder(MdnsAgent::Host* host) : MdnsAgent(host) {}

AddressResponder::~AddressResponder() {}

void AddressResponder::Start(const std::string& host_full_name, const MdnsAddresses& addresses) {
  FX_DCHECK(!host_full_name.empty());

  MdnsAgent::Start(host_full_name, addresses);
  host_full_name_ = host_full_name;
}

void AddressResponder::ReceiveQuestion(const DnsQuestion& question,
                                       const ReplyAddress& reply_address) {
  if ((question.type_ == DnsType::kA || question.type_ == DnsType::kAaaa ||
       question.type_ == DnsType::kAny) &&
      question.name_.dotted_string_ == host_full_name_) {
    SendAddresses(MdnsResourceSection::kAnswer, reply_address);
  }
}

}  // namespace mdns
