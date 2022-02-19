// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/mdns_deprecated_service_impl.h"

#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <unistd.h>

#include "lib/fidl/cpp/type_converter.h"
#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/lib/fsl/types/type_converters.h"

namespace fidl {

template <>
struct TypeConverter<mdns::Media, fuchsia::net::mdns::Media> {
  static mdns::Media Convert(fuchsia::net::mdns::Media value) {
    switch (value) {
      case fuchsia::net::mdns::Media::WIRED:
        return mdns::Media::kWired;
      case fuchsia::net::mdns::Media::WIRELESS:
        return mdns::Media::kWireless;
      default:
        FX_DCHECK(value ==
                  (fuchsia::net::mdns::Media::WIRED | fuchsia::net::mdns::Media::WIRELESS));
        return mdns::Media::kBoth;
    }
  }
};

template <>
struct TypeConverter<fuchsia::net::mdns::PublicationCause, mdns::Mdns::PublicationCause> {
  static fuchsia::net::mdns::PublicationCause Convert(mdns::Mdns::PublicationCause value) {
    switch (value) {
      case mdns::Mdns::PublicationCause::kAnnouncement:
        return fuchsia::net::mdns::PublicationCause::ANNOUNCEMENT;
      case mdns::Mdns::PublicationCause::kQueryMulticastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_MULTICAST_RESPONSE;
      case mdns::Mdns::PublicationCause::kQueryUnicastResponse:
        return fuchsia::net::mdns::PublicationCause::QUERY_UNICAST_RESPONSE;
    }
  }
};

}  // namespace fidl

namespace mdns {

MdnsDeprecatedServiceImpl::MdnsDeprecatedServiceImpl(Mdns& mdns,
                                                     sys::ComponentContext* component_context)
    : resolver_bindings_(this, "Resolver"),
      subscriber_bindings_(this, "Subscriber"),
      publisher_bindings_(this, "Publisher"),
      mdns_(mdns) {
  component_context->outgoing()->AddPublicService<fuchsia::net::mdns::Resolver>(
      fit::bind_member<&BindingSet<fuchsia::net::mdns::Resolver>::OnBindRequest>(
          &resolver_bindings_));
  component_context->outgoing()->AddPublicService<fuchsia::net::mdns::Subscriber>(
      fit::bind_member<&BindingSet<fuchsia::net::mdns::Subscriber>::OnBindRequest>(
          &subscriber_bindings_));
  component_context->outgoing()->AddPublicService<fuchsia::net::mdns::Publisher>(
      fit::bind_member<&BindingSet<fuchsia::net::mdns::Publisher>::OnBindRequest>(
          &publisher_bindings_));
}

MdnsDeprecatedServiceImpl::~MdnsDeprecatedServiceImpl() {}

void MdnsDeprecatedServiceImpl::OnReady() {
  resolver_bindings_.OnReady();
  subscriber_bindings_.OnReady();
  publisher_bindings_.OnReady();
}

void MdnsDeprecatedServiceImpl::ResolveHostName(std::string host, int64_t timeout_ns,
                                                ResolveHostNameCallback callback) {
  if (!MdnsNames::IsValidHostName(host)) {
    FX_LOGS(ERROR) << "ResolveHostName called with invalid host name " << host;
    callback(nullptr, nullptr);
    return;
  }

  mdns_.ResolveHostName(
      host, zx::clock::get_monotonic() + zx::nsec(timeout_ns),
      [callback = std::move(callback)](const std::string& host, const inet::IpAddress& v4_address,
                                       const inet::IpAddress& v6_address) {
        callback(v4_address ? std::make_unique<fuchsia::net::Ipv4Address>(
                                  MdnsFidlUtil::CreateIpv4Address(v4_address))
                            : nullptr,
                 v6_address ? std::make_unique<fuchsia::net::Ipv6Address>(
                                  MdnsFidlUtil::CreateIpv6Address(v6_address))
                            : nullptr);
      });
}

void MdnsDeprecatedServiceImpl::SubscribeToService(
    std::string service,
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber_handle) {
  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "ResolveHostName called with invalid service name " << service;
    return;
  }

  size_t id = next_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(std::move(subscriber_handle),
                                                 [this, id]() { subscribers_by_id_.erase(id); });

  mdns_.SubscribeToService(service, subscriber.get());

  subscribers_by_id_.emplace(id, std::move(subscriber));
}

void MdnsDeprecatedServiceImpl::PublishServiceInstance(
    std::string service, std::string instance, fuchsia::net::mdns::Media media, bool perform_probe,
    fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle,
    PublishServiceInstanceCallback callback) {
  FX_DCHECK(responder_handle);

  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "PublishServiceInstance called with invalid service name " << service;
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_SERVICE_NAME);
    callback(std::move(result));
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance)) {
    FX_LOGS(ERROR) << "PublishServiceInstance called with invalid instance name " << instance;
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_INSTANCE_NAME);
    callback(std::move(result));
    return;
  }

  if (media != fuchsia::net::mdns::Media::WIRED && media != fuchsia::net::mdns::Media::WIRELESS &&
      media != (fuchsia::net::mdns::Media::WIRED | fuchsia::net::mdns::Media::WIRELESS)) {
    FX_LOGS(ERROR) << "PublishServiceInstance called with invalid media "
                   << static_cast<uint32_t>(media);
    fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
    result.set_err(fuchsia::net::mdns::Error::INVALID_MEDIA);
    callback(std::move(result));
    return;
  }

  // TODO(fxbug.dev/56579): Review this approach to conflicts.
  std::string instance_full_name = MdnsNames::LocalInstanceFullName(instance, service);

  // If there's an existing publisher for this full name, destroy it so the new publication
  // supercedes the old one.
  publishers_by_instance_full_name_.erase(instance_full_name);

  auto responder_ptr = responder_handle.Bind();
  FX_DCHECK(responder_ptr);

  auto publisher = std::make_unique<ResponderPublisher>(
      std::move(responder_ptr), std::move(callback), [this, instance_full_name]() {
        publishers_by_instance_full_name_.erase(instance_full_name);
      });

  bool result = mdns_.PublishServiceInstance(service, instance, perform_probe,
                                             fidl::To<Media>(media), publisher.get());
  // Because of the erase call above, |PublishServiceInstance| should always succeed.
  FX_DCHECK(result);

  publishers_by_instance_full_name_.emplace(instance_full_name, std::move(publisher));
}

////////////////////////////////////////////////////////////////////////////////
// MdnsDeprecatedServiceImpl::Subscriber implementation

MdnsDeprecatedServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> handle, fit::closure deleter) {
  client_.Bind(std::move(handle));
  client_.set_error_handler([this, deleter = std::move(deleter)](zx_status_t status) mutable {
    // Clearing the error handler frees the capture list, so we need to save |deleter|.
    auto save_deleter = std::move(deleter);
    client_.set_error_handler(nullptr);
    client_.Unbind();
    save_deleter();
  });
}

MdnsDeprecatedServiceImpl::Subscriber::~Subscriber() {
  client_.set_error_handler(nullptr);
  if (client_.is_bound()) {
    client_.Unbind();
  }
}

void MdnsDeprecatedServiceImpl::Subscriber::InstanceDiscovered(
    const std::string& service, const std::string& instance, const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address, const std::vector<std::string>& text,
    uint16_t srv_priority, uint16_t srv_weight, const std::string& target) {
  Entry entry{.type = EntryType::kInstanceDiscovered};
  MdnsFidlUtil::FillServiceInstance(&entry.service_instance, service, instance, v4_address,
                                    v6_address, text, srv_priority, srv_weight, target);
  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void MdnsDeprecatedServiceImpl::Subscriber::InstanceChanged(
    const std::string& service, const std::string& instance, const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address, const std::vector<std::string>& text,
    uint16_t srv_priority, uint16_t srv_weight, const std::string& target) {
  Entry entry{.type = EntryType::kInstanceChanged};
  MdnsFidlUtil::FillServiceInstance(&entry.service_instance, service, instance, v4_address,
                                    v6_address, text, srv_priority, srv_weight, target);

  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void MdnsDeprecatedServiceImpl::Subscriber::InstanceLost(const std::string& service,
                                                         const std::string& instance) {
  Entry entry{.type = EntryType::kInstanceLost};
  entry.service_instance.set_service(service);
  entry.service_instance.set_instance(instance);
  entries_.push(std::move(entry));

  MaybeSendNextEntry();
}

void MdnsDeprecatedServiceImpl::Subscriber::Query(DnsType type_queried) {
  entries_.push({.type = EntryType::kQuery, .type_queried = type_queried});
  MaybeSendNextEntry();
}

void MdnsDeprecatedServiceImpl::Subscriber::MaybeSendNextEntry() {
  FX_DCHECK(pipeline_depth_ <= kMaxPipelineDepth);
  if (pipeline_depth_ == kMaxPipelineDepth || entries_.empty()) {
    return;
  }

  Entry& entry = entries_.front();
  auto on_reply = fit::bind_member<&MdnsDeprecatedServiceImpl::Subscriber::ReplyReceived>(this);

  FX_DCHECK(client_);
  switch (entry.type) {
    case EntryType::kInstanceDiscovered:
      client_->OnInstanceDiscovered(std::move(entry.service_instance), std::move(on_reply));
      break;
    case EntryType::kInstanceChanged:
      client_->OnInstanceChanged(std::move(entry.service_instance), std::move(on_reply));
      break;
    case EntryType::kInstanceLost:
      client_->OnInstanceLost(entry.service_instance.service(), entry.service_instance.instance(),
                              std::move(on_reply));
      break;
    case EntryType::kQuery:
      client_->OnQuery(MdnsFidlUtil::Convert(entry.type_queried), std::move(on_reply));
      break;
  }

  ++pipeline_depth_;
  entries_.pop();
}

void MdnsDeprecatedServiceImpl::Subscriber::ReplyReceived() {
  FX_DCHECK(pipeline_depth_ != 0);
  --pipeline_depth_;
  MaybeSendNextEntry();
}

////////////////////////////////////////////////////////////////////////////////
// MdnsDeprecatedServiceImpl::ResponderPublisher implementation

MdnsDeprecatedServiceImpl::ResponderPublisher::ResponderPublisher(
    fuchsia::net::mdns::PublicationResponderPtr responder, PublishServiceInstanceCallback callback,
    fit::closure deleter)
    : responder_(std::move(responder)), callback_(std::move(callback)) {
  FX_DCHECK(responder_);

  responder_.set_error_handler([this, deleter = std::move(deleter)](zx_status_t status) mutable {
    // Clearing the error handler frees the capture list, so we need to save |deleter|.
    auto save_deleter = std::move(deleter);
    responder_.set_error_handler(nullptr);
    save_deleter();
  });

  responder_.events().SetSubtypes = [this](std::vector<std::string> subtypes) {
    for (auto& subtype : subtypes) {
      if (!MdnsNames::IsValidSubtypeName(subtype)) {
        FX_LOGS(ERROR) << "Invalid subtype " << subtype
                       << " passed in SetSubtypes event, closing connection.";
        // TODO(fxb/71542): Should also call deleter here and at other Unpublish call sites.
        responder_ = nullptr;
        Unpublish();
        return;
      }
    }

    SetSubtypes(std::move(subtypes));
  };

  responder_.events().Reannounce = [this]() { Reannounce(); };
}

MdnsDeprecatedServiceImpl::ResponderPublisher::~ResponderPublisher() {
  responder_.set_error_handler(nullptr);
  if (responder_.is_bound()) {
    responder_.Unbind();
  }
}

void MdnsDeprecatedServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  FX_DCHECK(callback_);

  fuchsia::net::mdns::Publisher_PublishServiceInstance_Result result;
  if (success) {
    result.set_response(fuchsia::net::mdns::Publisher_PublishServiceInstance_Response());
  } else {
    result.set_err(fuchsia::net::mdns::Error::ALREADY_PUBLISHED_ON_SUBNET);
  }

  callback_(std::move(result));
}

void MdnsDeprecatedServiceImpl::ResponderPublisher::GetPublication(
    Mdns::PublicationCause publication_cause, const std::string& subtype,
    const std::vector<inet::SocketAddress>& source_addresses, GetPublicationCallback callback) {
  if (on_publication_calls_in_progress_ < kMaxOnPublicationCallsInProgress) {
    ++on_publication_calls_in_progress_;
    GetPublicationNow(publication_cause, subtype, source_addresses, std::move(callback));
  } else {
    pending_publications_.emplace(publication_cause, subtype, source_addresses,
                                  std::move(callback));
  }
}

void MdnsDeprecatedServiceImpl::ResponderPublisher::OnGetPublicationComplete() {
  --on_publication_calls_in_progress_;
  if (!pending_publications_.empty() &&
      on_publication_calls_in_progress_ < kMaxOnPublicationCallsInProgress) {
    ++on_publication_calls_in_progress_;
    auto& entry = pending_publications_.front();
    GetPublicationNow(entry.publication_cause_, entry.subtype_, entry.source_addresses_,
                      std::move(entry.callback_));
    // Note that if |GetPublicationNow| calls this method back synchronously, the pop call below
    // would happen too late. However, the calls happen asynchronously, so we're ok.
    pending_publications_.pop();
  }
}

void MdnsDeprecatedServiceImpl::ResponderPublisher::GetPublicationNow(
    Mdns::PublicationCause publication_cause, const std::string& subtype,
    const std::vector<inet::SocketAddress>& source_addresses, GetPublicationCallback callback) {
  FX_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));

  FX_DCHECK(responder_);
  responder_->OnPublication(
      fidl::To<fuchsia::net::mdns::PublicationCause>(publication_cause), subtype,
      MdnsFidlUtil::Convert(source_addresses),
      [this, callback = std::move(callback)](fuchsia::net::mdns::PublicationPtr publication_ptr) {
        if (publication_ptr) {
          for (auto& text : publication_ptr->text) {
            if (!MdnsNames::IsValidTextString(text)) {
              FX_LOGS(ERROR) << "Invalid text string returned by "
                                "Responder.GetPublication, closing connection.";
              responder_ = nullptr;
              Unpublish();
              return;
            }
          }

          if (publication_ptr->ptr_ttl < ZX_SEC(1) || publication_ptr->srv_ttl < ZX_SEC(1) ||
              publication_ptr->txt_ttl < ZX_SEC(1)) {
            FX_LOGS(ERROR) << "TTL less than one second returned by "
                              "Responder.GetPublication, closing connection.";
            responder_ = nullptr;
            Unpublish();
            return;
          }
        }

        callback(MdnsFidlUtil::Convert(publication_ptr));
        OnGetPublicationComplete();
      });
}

}  // namespace mdns
