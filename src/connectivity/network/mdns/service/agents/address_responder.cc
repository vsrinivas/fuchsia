// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_responder.h"

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

AddressResponder::AddressResponder(MdnsAgent::Owner* owner, Media media, IpVersions ip_versions)
    : MdnsAgent(owner), media_(media), ip_versions_(ip_versions) {}

AddressResponder::AddressResponder(MdnsAgent::Owner* owner, std::string host_full_name,
                                   std::vector<inet::IpAddress> addresses, Media media,
                                   IpVersions ip_versions)
    : MdnsAgent(owner),
      host_full_name_(std::move(host_full_name)),
      addresses_(std::move(addresses)),
      media_(media),
      ip_versions_(ip_versions) {
  FX_DCHECK(!host_full_name_.empty());
  // TODO(fxb/113901): Restore this check when alt_services is no longer needed.
  // FX_DCHECK(!addresses_.empty());
}

AddressResponder::~AddressResponder() {}

std::vector<HostAddress> AddressResponder::addresses() const {
  return fidl::To<std::vector<HostAddress>>(addresses_);
}

void AddressResponder::Start(const std::string& local_host_full_name) {
  FX_DCHECK(!local_host_full_name.empty());

  MdnsAgent::Start(local_host_full_name);

  if (host_full_name_.empty()) {
    host_full_name_ = local_host_full_name;
  }
}

void AddressResponder::ReceiveQuestion(const DnsQuestion& question,
                                       const ReplyAddress& reply_address,
                                       const ReplyAddress& sender_address) {
  if (sender_address.Matches(media_) && sender_address.Matches(ip_versions_) &&
      (question.type_ == DnsType::kA || question.type_ == DnsType::kAaaa ||
       question.type_ == DnsType::kAny) &&
      question.name_.dotted_string_ == host_full_name_) {
    MaybeSendAddresses(reply_address);
  }
}

void AddressResponder::MaybeSendAddresses(ReplyAddress reply_address) {
  // We only throttle multicast sends. A V4 multicast reply address indicates V4 and V6 multicast.
  if (reply_address.is_multicast_placeholder()) {
    // Replace the general multicast placeholder with one that's restricted to the desired |Media|
    // and |IpVersions|.
    reply_address = ReplyAddress::Multicast(media_, ip_versions_);

    if (throttle_state_ == kThrottleStatePending) {
      // The send is already happening.
      return;
    }

    if (throttle_state_ + kMinMulticastInterval > now()) {
      // A send happened less than a second ago, and no send is currently scheduled. We need to
      // schedule a multicast send for one second after the previous one.
      PostTaskForTime(
          [this, reply_address]() {
            SendAddressResources(reply_address);
            throttle_state_ = now();
          },
          throttle_state_ + kMinMulticastInterval);

      throttle_state_ = kThrottleStatePending;
      return;
    }
  }

  SendAddressResources(reply_address);

  throttle_state_ = now();
}

void AddressResponder::SendAddressResources(ReplyAddress reply_address) {
  if (addresses_.empty()) {
    // Send local addresses. The address value in the resource is invalid, which tells the interface
    // transceivers to send their own addresses.
    SendResource(std::make_shared<DnsResource>(host_full_name_, DnsType::kA),
                 MdnsResourceSection::kAnswer, reply_address);
  } else {
    // Send addresses that were provided in the constructor.
    for (const auto& address : addresses_) {
      SendResource(std::make_shared<DnsResource>(host_full_name_, address),
                   MdnsResourceSection::kAnswer, reply_address);
    }
  }
}

}  // namespace mdns
