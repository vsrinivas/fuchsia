// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/responder.h"

#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

Responder::Responder(MdnsAgent::Host* host,
                     const std::string& host_full_name,
                     const std::string& service_name,
                     const std::string& instance_name,
                     const std::vector<std::string>& announced_subtypes,
                     fidl::InterfaceHandle<MdnsResponder> responder_handle)
    : host_(host),
      host_full_name_(host_full_name),
      service_name_(service_name),
      instance_name_(instance_name),
      instance_full_name_(
          MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      announced_subtypes_(announced_subtypes),
      responder_(MdnsResponderPtr::Create(std::move(responder_handle))) {
  responder_.set_connection_error_handler([this]() {
    responder_.set_connection_error_handler(nullptr);
    responder_.reset();
    host_->RemoveAgent(this, instance_full_name_);
  });
}

Responder::Responder(MdnsAgent::Host* host,
                     const std::string& host_full_name,
                     const std::string& service_name,
                     const std::string& instance_name,
                     MdnsPublicationPtr publication)
    : host_(host),
      host_full_name_(host_full_name),
      service_name_(service_name),
      instance_name_(instance_name),
      instance_full_name_(
          MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      publication_(std::move(publication)) {}

Responder::~Responder() {}

void Responder::Start() {
  Wake();
}

void Responder::Wake() {
  GetAndSendPublication(false, "");

  for (const std::string& subtype : announced_subtypes_) {
    GetAndSendPublication(false, subtype);
  }

  if (announcement_interval_ > kMaxAnnouncementInterval) {
    return;
  }

  host_->WakeAt(shared_from_this(),
                fxl::TimePoint::Now() +
                    fxl::TimeDelta::FromSeconds(announcement_interval_));

  announcement_interval_ *= 2;
}

void Responder::ReceiveQuestion(const DnsQuestion& question) {
  std::string name = question.name_.dotted_string_;
  std::string subtype;

  switch (question.type_) {
    case DnsType::kPtr:
      if (MdnsNames::MatchServiceName(name, service_name_, &subtype)) {
        GetAndSendPublication(true, subtype);
      }
      break;
    case DnsType::kSrv:
    case DnsType::kTxt:
      if (question.name_.dotted_string_ == instance_full_name_) {
        GetAndSendPublication(true, "");
      }
      break;
    default:
      break;
  }
}

void Responder::ReceiveResource(const DnsResource& resource,
                                MdnsResourceSection section) {}

void Responder::EndOfMessage() {}

void Responder::Quit() {
  if (publication_) {
    // Send epitaph.
    publication_->text.reset();
    publication_->ptr_ttl_seconds = 0;
    publication_->srv_ttl_seconds = 0;
    publication_->txt_ttl_seconds = 0;
    SendPublication("", *publication_);
  }

  host_->RemoveAgent(this, instance_full_name_);
}

void Responder::GetAndSendPublication(bool query,
                                      const std::string& subtype) const {
  if (responder_) {
    responder_->GetPublication(
        query, subtype.empty() ? fidl::String() : fidl::String(subtype),
        [this, subtype](MdnsPublicationPtr publication) {
          if (publication) {
            SendPublication(subtype, *publication);
          }
        });
    return;
  }

  FXL_DCHECK(publication_);
  if (subtype.empty()) {
    SendPublication(subtype, *publication_);
  }
}

void Responder::SendPublication(const std::string& subtype,
                                const MdnsPublication& publication) const {
  std::string service_full_name =
      subtype.empty()
          ? MdnsNames::LocalServiceFullName(service_name_)
          : MdnsNames::LocalServiceSubtypeFullName(service_name_, subtype);

  auto ptr_resource =
      std::make_shared<DnsResource>(service_full_name, DnsType::kPtr);
  ptr_resource->time_to_live_ = publication.ptr_ttl_seconds;
  ptr_resource->ptr_.pointer_domain_name_ = instance_full_name_;
  host_->SendResource(ptr_resource, MdnsResourceSection::kAnswer);

  auto srv_resource =
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv);
  srv_resource->time_to_live_ = publication.srv_ttl_seconds;
  srv_resource->srv_.port_ = IpPort::From_uint16_t(publication.port);
  srv_resource->srv_.target_ = host_full_name_;
  host_->SendResource(srv_resource, MdnsResourceSection::kAdditional);

  auto txt_resource =
      std::make_shared<DnsResource>(instance_full_name_, DnsType::kTxt);
  txt_resource->time_to_live_ = publication.txt_ttl_seconds;
  txt_resource->txt_.strings_ = publication.text.To<std::vector<std::string>>();
  host_->SendResource(txt_resource, MdnsResourceSection::kAdditional);

  host_->SendAddresses(MdnsResourceSection::kAdditional);
}

}  // namespace mdns
}  // namespace netconnector
