// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/instance_responder.h"

#include <algorithm>

#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

InstanceResponder::InstanceResponder(
    MdnsAgent::Host* host,
    const std::string& service_name,
    const std::string& instance_name,
    fidl::InterfaceHandle<MdnsResponder> responder_handle)
    : MdnsAgent(host),
      service_name_(service_name),
      instance_name_(instance_name),
      instance_full_name_(
          MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      mdns_responder_(MdnsResponderPtr::Create(std::move(responder_handle))) {
  mdns_responder_.set_connection_error_handler([this]() {
    mdns_responder_.set_connection_error_handler(nullptr);
    mdns_responder_.reset();
    RemoveSelf(instance_full_name_);
  });
}

InstanceResponder::InstanceResponder(MdnsAgent::Host* host,
                                     const std::string& service_name,
                                     const std::string& instance_name,
                                     MdnsPublicationPtr publication,
                                     const PublishCallback& callback)
    : MdnsAgent(host),
      service_name_(service_name),
      instance_name_(instance_name),
      instance_full_name_(
          MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      publication_(std::move(publication)),
      callback_(callback) {}

InstanceResponder::~InstanceResponder() {}

void InstanceResponder::Start(const std::string& host_full_name) {
  FXL_DCHECK(!host_full_name.empty());

  host_full_name_ = host_full_name;

  Reannounce();
}

void InstanceResponder::ReceiveQuestion(const DnsQuestion& question,
                                        const ReplyAddress& reply_address) {
  std::string name = question.name_.dotted_string_;
  std::string subtype;

  switch (question.type_) {
    case DnsType::kPtr:
      if (MdnsNames::MatchServiceName(name, service_name_, &subtype)) {
        GetAndSendPublication(true, subtype, reply_address);
      }
      break;
    case DnsType::kSrv:
    case DnsType::kTxt:
      if (question.name_.dotted_string_ == instance_full_name_) {
        GetAndSendPublication(true, "", reply_address);
      }
      break;
    case DnsType::kAny:
      if (question.name_.dotted_string_ == instance_full_name_ ||
          MdnsNames::MatchServiceName(name, service_name_, &subtype)) {
        GetAndSendPublication(true, subtype, reply_address);
      }
      break;
    default:
      break;
  }
}

void InstanceResponder::Quit() {
  if (publication_) {
    SendGoodbye(std::move(publication_));
    RemoveSelf(instance_full_name_);
    return;
  }

  should_quit_ = true;
  GetAndSendPublication(false);
}

void InstanceResponder::UpdateStatus(MdnsResult result) {
  if (mdns_responder_) {
    mdns_responder_->UpdateStatus(result);
  } else if (callback_) {
    callback_(result);
    callback_ = nullptr;
  }
}

void InstanceResponder::SetSubtypes(std::vector<std::string> subtypes) {
  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now. The first announcement
  // contains PTR records for the removed subtypes with TTL of zero.
  for (const std::string& subtype : subtypes_) {
    if (std::find(subtypes.begin(), subtypes.end(), subtype) ==
        subtypes.end()) {
      SendSubtypePtrRecord(subtype, 0);
    }
  }

  subtypes_ = std::move(subtypes);

  Reannounce();
}

void InstanceResponder::Reannounce() {
  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now.
  announcement_interval_ = kInitialAnnouncementInterval;
  SendAnnouncement();
}

void InstanceResponder::SendAnnouncement() {
  GetAndSendPublication(false);

  for (const std::string& subtype : subtypes_) {
    SendSubtypePtrRecord(subtype);
  }

  if (announcement_interval_ > kMaxAnnouncementInterval) {
    return;
  }

  PostTaskForTime([this]() { SendAnnouncement(); },
                  fxl::TimePoint::Now() + announcement_interval_);

  announcement_interval_ = announcement_interval_ * 2;
}

void InstanceResponder::GetAndSendPublication(
    bool query,
    const std::string& subtype,
    const ReplyAddress& reply_address) const {
  if (mdns_responder_) {
    mdns_responder_->GetPublication(
        query, subtype.empty() ? fidl::String() : fidl::String(subtype),
        [ this, subtype,
          reply_address = reply_address ](MdnsPublicationPtr publication) {
          if (should_quit_) {
            if (publication) {
              SendGoodbye(std::move(publication));
            }

            RemoveSelf(instance_full_name_);
            return;
          }

          if (publication) {
            SendPublication(*publication, subtype, reply_address);
          }
        });

    return;
  }

  FXL_DCHECK(publication_);
  if (subtype.empty()) {
    SendPublication(*publication_, subtype, reply_address);
  }
}

void InstanceResponder::SendPublication(
    const MdnsPublication& publication,
    const std::string& subtype,
    const ReplyAddress& reply_address) const {
  if (!subtype.empty()) {
    SendSubtypePtrRecord(subtype, publication.ptr_ttl_seconds, reply_address);
  }

  auto ptr_resource = std::make_shared<DnsResource>(
      MdnsNames::LocalServiceFullName(service_name_), DnsType::kPtr);
  ptr_resource->time_to_live_ = publication.ptr_ttl_seconds;
  ptr_resource->ptr_.pointer_domain_name_ = instance_full_name_;
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);

  auto srv_resource =
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv);
  srv_resource->time_to_live_ = publication.srv_ttl_seconds;
  srv_resource->srv_.port_ = IpPort::From_uint16_t(publication.port);
  srv_resource->srv_.target_ = host_full_name_;
  SendResource(srv_resource, MdnsResourceSection::kAdditional, reply_address);

  auto txt_resource =
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kTxt);
  txt_resource->time_to_live_ = publication.txt_ttl_seconds;
  txt_resource->txt_.strings_ = publication.text.To<std::vector<std::string>>();
  SendResource(txt_resource, MdnsResourceSection::kAdditional, reply_address);

  SendAddresses(MdnsResourceSection::kAdditional, reply_address);
}

void InstanceResponder::SendSubtypePtrRecord(
    const std::string& subtype,
    uint32_t ttl,
    const ReplyAddress& reply_address) const {
  FXL_DCHECK(!subtype.empty());

  auto ptr_resource = std::make_shared<DnsResource>(
      MdnsNames::LocalServiceSubtypeFullName(service_name_, subtype),
      DnsType::kPtr);
  ptr_resource->time_to_live_ = ttl;
  ptr_resource->ptr_.pointer_domain_name_ = instance_full_name_;
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);
}

void InstanceResponder::SendGoodbye(MdnsPublicationPtr publication) const {
  FXL_DCHECK(publication);

  // TXT will be sent, but with no strings.
  publication_->text.reset();

  publication_->ptr_ttl_seconds = 0;
  publication_->srv_ttl_seconds = 0;
  publication_->txt_ttl_seconds = 0;

  SendPublication(*publication_);
}

}  // namespace mdns
}  // namespace netconnector
