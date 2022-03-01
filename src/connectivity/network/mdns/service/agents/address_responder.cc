// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_responder.h"

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

AddressResponder::AddressResponder(MdnsAgent::Owner* host) : MdnsAgent(host) {}

AddressResponder::~AddressResponder() {}

void AddressResponder::Start(const std::string& local_host_full_name) {
  FX_DCHECK(!local_host_full_name.empty());

  MdnsAgent::Start(local_host_full_name);
  host_full_name_ = local_host_full_name;
}

void AddressResponder::ReceiveQuestion(const DnsQuestion& question,
                                       const ReplyAddress& reply_address,
                                       const ReplyAddress& sender_address) {
  if ((question.type_ == DnsType::kA || question.type_ == DnsType::kAaaa ||
       question.type_ == DnsType::kAny) &&
      question.name_.dotted_string_ == host_full_name_) {
    MaybeSendAddresses(reply_address);
  }
}

void AddressResponder::MaybeSendAddresses(const ReplyAddress& reply_address) {
  // We only throttle multicast sends. A V4 multicast reply address indicates V4 and V6 multicast.
  if (reply_address.socket_address() == MdnsAddresses::v4_multicast()) {
    if (throttle_state_ == kThrottleStatePending) {
      // The send is already happening.
      return;
    }

    if (throttle_state_ + kMinMulticastInterval > now()) {
      // A send happened less than a second ago, and no send is currently scheduled. We need to
      // schedule a multicast send for one second after the previous one.
      PostTaskForTime(
          [this, reply_address]() {
            SendAddresses(MdnsResourceSection::kAnswer, reply_address);
            throttle_state_ = now();
          },
          throttle_state_ + kMinMulticastInterval);

      throttle_state_ = kThrottleStatePending;
      return;
    }
  }

  SendAddresses(MdnsResourceSection::kAnswer, reply_address);
  throttle_state_ = now();
}

}  // namespace mdns
