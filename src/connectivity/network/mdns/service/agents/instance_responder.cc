// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/instance_responder.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"

namespace mdns {

InstanceResponder::InstanceResponder(MdnsAgent::Owner* owner, std::string host_name,
                                     std::vector<inet::IpAddress> addresses,
                                     std::string service_name, std::string instance_name,
                                     Media media, IpVersions ip_versions,
                                     Mdns::Publisher* publisher)
    : MdnsAgent(owner),
      host_full_name_(host_name.empty() ? host_name : MdnsNames::HostFullName(host_name)),
      addresses_(std::move(addresses)),
      instance_full_name_(MdnsNames::InstanceFullName(instance_name, service_name)),
      media_(media),
      ip_versions_(ip_versions),
      publisher_(publisher) {
  // TODO(fxb/113901): Restore this check when alt_services is no longer needed.
  // FX_DCHECK(host_name.empty() == addresses_.empty());
  instance_.service_name_ = service_name;
  instance_.instance_name_ = instance_name;
}

InstanceResponder::~InstanceResponder() {}

void InstanceResponder::Start(const std::string& local_host_full_name) {
  FX_DCHECK(!local_host_full_name.empty());

  MdnsAgent::Start(local_host_full_name);

  if (host_full_name_.empty()) {
    host_full_name_ = local_host_full_name;
  }

  instance_.target_name_ = MdnsNames::HostNameFromFullName(host_full_name_);

  Reannounce();
}

void InstanceResponder::ReceiveQuestion(const DnsQuestion& question,
                                        const ReplyAddress& reply_address,
                                        const ReplyAddress& sender_address) {
  std::string name = question.name_.dotted_string_;
  std::string subtype;

  if (!sender_address.Matches(media_) || !sender_address.Matches(ip_versions_)) {
    // Question received via unsupported medium or ip version. Ignore.
    return;
  }

  // We infer publication cause from the reply address. Announcements don't use this path, and
  // there are cases in which we send unicast even though the unicast bit is not set on the question
  // (specifically, when the reply address port isn't 5353).
  auto publication_cause = reply_address.is_multicast_placeholder()
                               ? PublicationCause::kQueryMulticastResponse
                               : PublicationCause::kQueryUnicastResponse;

  switch (question.type_) {
    case DnsType::kPtr:
      if (MdnsNames::MatchServiceName(name, instance_.service_name_, &subtype)) {
        LogSenderAddress(sender_address);
        MaybeGetAndSendPublication(publication_cause, subtype, Constrain(reply_address));
      } else if (question.name_.dotted_string_ == MdnsNames::kAnyServiceFullName) {
        SendAnyServiceResponse(Constrain(reply_address));
      }
      break;
    case DnsType::kSrv:
    case DnsType::kTxt:
      if (question.name_.dotted_string_ == instance_full_name_) {
        LogSenderAddress(sender_address);
        MaybeGetAndSendPublication(publication_cause, "", Constrain(reply_address));
      }
      break;
    case DnsType::kAny:
      if (question.name_.dotted_string_ == instance_full_name_ ||
          MdnsNames::MatchServiceName(name, instance_.service_name_, &subtype)) {
        LogSenderAddress(sender_address);
        MaybeGetAndSendPublication(publication_cause, subtype, Constrain(reply_address));
      }
      break;
    default:
      break;
  }
}

void InstanceResponder::Quit() {
  if (started()) {
    SendGoodbye();
  }

  publisher_ = nullptr;

  MdnsAgent::Quit();
}

void InstanceResponder::OnLocalHostAddressesChanged() {
  if (from_proxy() || !port_.is_valid()) {
    return;
  }

  UpdateInstanceAddresses();
  ChangeLocalServiceInstance(instance_, false);
}

void InstanceResponder::SetSubtypes(std::vector<std::string> subtypes) {
  if (!started()) {
    // This agent isn't started, so we can't announce yet. There's no need to
    // remove old subtypes, because no subtypes have been announced yet.
    // |Reannounce| will be called by |Start|.
    subtypes_ = std::move(subtypes);
    return;
  }

  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now. The first announcement
  // contains PTR records for the removed subtypes with TTL of zero.
  for (const std::string& subtype : subtypes_) {
    if (std::find(subtypes.begin(), subtypes.end(), subtype) == subtypes.end()) {
      SendSubtypePtrRecord(subtype, 0, multicast_reply());
    }
  }

  subtypes_ = std::move(subtypes);

  Reannounce();
}

void InstanceResponder::Reannounce() {
  if (!started()) {
    // This agent isn't started, so we can't announce yet. |Reannounce| will be called by |Start|.
    return;
  }

  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now.
  announcement_interval_ = kInitialAnnouncementInterval;
  SendAnnouncement();
}

void InstanceResponder::LogSenderAddress(const ReplyAddress& sender_address) {
  if (sender_addresses_.size() == kMaxSenderAddresses) {
    // We only record up to 64 addresses. This limit should rarely be hit, because sender addresses
    // only accumulate when we're throttling over a one-second interval. Most of the time, we'll log
    // only one sender address before calling |GetPublication| and clearing the list.
    return;
  }

  sender_addresses_.push_back(sender_address.socket_address());
}

void InstanceResponder::SendAnnouncement() {
  GetAndSendPublication(PublicationCause::kAnnouncement, "", multicast_reply());

  for (const std::string& subtype : subtypes_) {
    SendSubtypePtrRecord(subtype, DnsResource::kShortTimeToLive, multicast_reply());
  }

  if (announcement_interval_ > kMaxAnnouncementInterval) {
    return;
  }

  PostTaskForTime([this]() { SendAnnouncement(); }, now() + announcement_interval_);

  announcement_interval_ = announcement_interval_ * 2;
}

void InstanceResponder::SendAnyServiceResponse(const ReplyAddress& reply_address) {
  auto ptr_resource = std::make_shared<DnsResource>(MdnsNames::kAnyServiceFullName, DnsType::kPtr);
  ptr_resource->ptr_.pointer_domain_name_ =
      DnsName(MdnsNames::ServiceFullName(instance_.service_name_));
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);
}

void InstanceResponder::MaybeGetAndSendPublication(PublicationCause publication_cause,
                                                   const std::string& subtype,
                                                   const ReplyAddress& reply_address) {
  if (publisher_ == nullptr) {
    return;
  }

  // We only throttle multicast sends.
  if (publication_cause == PublicationCause::kQueryMulticastResponse) {
    zx::time throttle_state = kThrottleStateIdle;
    auto iter = throttle_state_by_subtype_.find(subtype);
    if (iter != throttle_state_by_subtype_.end()) {
      throttle_state = iter->second;
    }

    if (throttle_state == kThrottleStatePending) {
      // The send is already happening.
      return;
    }

    // We're either going to send now or schedule a send. In either case, a send is pending.
    throttle_state_by_subtype_[subtype] = kThrottleStatePending;

    if (throttle_state + kMinMulticastInterval > now()) {
      // A multicast publication of this subtype was sent less than a second ago, and no send is
      // currently scheduled. We need to schedule a multicast send for one second after the
      // previous one.
      PostTaskForTime(
          [this, publication_cause, subtype, reply_address]() {
            GetAndSendPublication(publication_cause, subtype, reply_address);
          },
          throttle_state + kMinMulticastInterval);
      return;
    }

    // We fall through here when throttling isn't in effect, and the response should be sent
    // immediately.
  }

  GetAndSendPublication(publication_cause, subtype, reply_address);
}

void InstanceResponder::GetAndSendPublication(PublicationCause publication_cause,
                                              const std::string& subtype,
                                              const ReplyAddress& reply_address) {
  if (publisher_ == nullptr) {
    return;
  }

  bool query = publication_cause != PublicationCause::kAnnouncement;

  publisher_->GetPublication(
      publication_cause, subtype, sender_addresses_,
      [this, query, subtype, reply_address](std::unique_ptr<Mdns::Publication> publication) {
        if (publication) {
          SendPublication(*publication, subtype, reply_address);
          // Make sure messages get sent immediately if this callback happens asynchronously
          // with respect to |ReceiveQuestion| or posted task execution.
          FlushSentItems();

          // A V4 multicast reply address indicates V4 and V6 multicast.
          if (query && reply_address.is_multicast_placeholder()) {
            throttle_state_by_subtype_[subtype] = now();
            // Remove the entry from |throttle_state_by_subtype_| later to prevent the map from
            // growing indefinitely.
            PostTaskForTime([this, subtype]() { IdleCheck(subtype); }, now() + kIdleCheckInterval);
          }
        }
      });

  sender_addresses_.clear();
}

void InstanceResponder::SendPublication(const Mdns::Publication& publication,
                                        const std::string& subtype,
                                        const ReplyAddress& reply_address) {
  if (!subtype.empty()) {
    SendSubtypePtrRecord(subtype, publication.ptr_ttl_seconds_, reply_address);
  }

  auto ptr_resource = std::make_shared<DnsResource>(
      MdnsNames::ServiceFullName(instance_.service_name_), DnsType::kPtr);
  ptr_resource->time_to_live_ = publication.ptr_ttl_seconds_;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName(instance_full_name_);
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);

  auto srv_resource = std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv);
  srv_resource->time_to_live_ = publication.srv_ttl_seconds_;
  srv_resource->srv_.priority_ = publication.srv_priority_;
  srv_resource->srv_.weight_ = publication.srv_weight_;
  srv_resource->srv_.port_ = publication.port_;
  srv_resource->srv_.target_ = DnsName(host_full_name_);
  SendResource(srv_resource, MdnsResourceSection::kAdditional, reply_address);

  auto txt_resource = std::make_shared<DnsResource>(instance_full_name_, DnsType::kTxt);
  txt_resource->time_to_live_ = publication.txt_ttl_seconds_;
  txt_resource->txt_.strings_ = publication.text_;
  SendResource(txt_resource, MdnsResourceSection::kAdditional, reply_address);

  if (addresses_.empty()) {
    // Send local addresses. The address value in the resource is invalid, which tells the interface
    // transceivers to send their own addresses.
    SendResource(std::make_shared<DnsResource>(host_full_name_, DnsType::kA),
                 MdnsResourceSection::kAdditional, reply_address);
  } else {
    // Send addresses that were provided in the constructor.
    for (const auto& address : addresses_) {
      SendResource(std::make_shared<DnsResource>(host_full_name_, address),
                   MdnsResourceSection::kAdditional, reply_address);
    }
  }

  if (!subtype.empty()) {
    return;
  }

  bool changed = false;

  if (port_ != publication.port_) {
    changed = true;
    port_ = publication.port_;
    UpdateInstanceAddresses();
  }

  if (instance_.text_ != publication.text_) {
    instance_.text_ = publication.text_;
    changed = true;
  }

  if (instance_.srv_priority_ != publication.srv_priority_) {
    instance_.srv_priority_ = publication.srv_priority_;
    changed = true;
  }

  if (instance_.srv_weight_ != publication.srv_weight_) {
    instance_.srv_weight_ = publication.srv_weight_;
    changed = true;
  }

  if (!changed) {
    return;
  }

  if (instance_ready_) {
    ChangeLocalServiceInstance(instance_, from_proxy());
  } else {
    instance_ready_ = true;
    AddLocalServiceInstance(instance_, from_proxy());
  }
}

void InstanceResponder::SendSubtypePtrRecord(const std::string& subtype, uint32_t ttl,
                                             const ReplyAddress& reply_address) const {
  FX_DCHECK(!subtype.empty());

  auto ptr_resource = std::make_shared<DnsResource>(
      MdnsNames::ServiceSubtypeFullName(instance_.service_name_, subtype), DnsType::kPtr);
  ptr_resource->time_to_live_ = ttl;
  ptr_resource->ptr_.pointer_domain_name_ = DnsName(instance_full_name_);
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);
}

void InstanceResponder::SendGoodbye() {
  Mdns::Publication publication;
  publication.ptr_ttl_seconds_ = 0;
  publication.srv_ttl_seconds_ = 0;
  publication.txt_ttl_seconds_ = 0;

  SendPublication(publication, "", multicast_reply());
}

void InstanceResponder::IdleCheck(const std::string& subtype) {
  auto iter = throttle_state_by_subtype_.find(subtype);
  if (iter != throttle_state_by_subtype_.end() && iter->second + kMinMulticastInterval < now()) {
    throttle_state_by_subtype_.erase(iter);
  }
}

void InstanceResponder::UpdateInstanceAddresses() {
  if (!port_.is_valid()) {
    return;
  }

  instance_.addresses_.clear();

  if (!addresses_.empty()) {
    for (auto addr : addresses_) {
      instance_.addresses_.emplace_back(inet::SocketAddress(addr, port_, 0));
    }
  } else {
    auto host_addresses = local_host_addresses();
    std::transform(host_addresses.begin(), host_addresses.end(),
                   std::back_inserter(instance_.addresses_), [this](const HostAddress& address) {
                     return inet::SocketAddress(address.address(), port_, address.interface_id());
                   });
  }
}

}  // namespace mdns
