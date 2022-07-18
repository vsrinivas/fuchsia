// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/service_instance_publisher_service_impl.h"

#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

ServiceInstancePublisherServiceImpl::ServiceInstancePublisherServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::ServiceInstancePublisher>(mdns, std::move(request),
                                                                    std::move(deleter)),
      default_media_(Media::kBoth),
      default_ip_versions_(IpVersions::kBoth) {}

ServiceInstancePublisherServiceImpl::ServiceInstancePublisherServiceImpl(
    Mdns& mdns, std::string host_name, std::vector<inet::IpAddress> addresses, Media default_media,
    IpVersions default_ip_versions,
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::ServiceInstancePublisher>(mdns, std::move(request),
                                                                    std::move(deleter)),
      host_name_(std::move(host_name)),
      addresses_(std::move(addresses)),
      default_media_(default_media),
      default_ip_versions_(default_ip_versions) {}

void ServiceInstancePublisherServiceImpl::PublishServiceInstance(
    std::string service, std::string instance,
    fuchsia::net::mdns::ServiceInstancePublicationOptions options,
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceInstancePublicationResponder>
        publication_responder,
    PublishServiceInstanceCallback callback) {
  FX_DCHECK(publication_responder);

  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "PublishServiceInstance called with invalid service name " << service
                   << ", closing connection.";
    Quit(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance)) {
    FX_LOGS(ERROR) << "PublishServiceInstance called with invalid instance name " << instance
                   << ", closing connection.";
    Quit(ZX_ERR_INVALID_ARGS);
    return;
  }

  Media media = options.has_media() ? fidl::To<Media>(options.media()) : default_media_;
  IpVersions ip_versions = options.has_ip_versions() ? fidl::To<IpVersions>(options.ip_versions())
                                                     : default_ip_versions_;
  bool perform_probe = options.has_perform_probe() ? options.perform_probe() : true;

  auto publisher = new ResponderPublisher(publication_responder.Bind(), callback.share());

  if (!mdns().PublishServiceInstance(host_name_, addresses_, service, instance, media, ip_versions,
                                     perform_probe, publisher)) {
    delete publisher;
    callback(fpromise::error(
        fuchsia::net::mdns::PublishServiceInstanceError::ALREADY_PUBLISHED_LOCALLY));
    return;
  }
}

////////////////////////////////////////////////////////////////////////////////
// ServiceInstancePublisherServiceImpl::ResponderPublisher implementation

ServiceInstancePublisherServiceImpl::ResponderPublisher::ResponderPublisher(
    fuchsia::net::mdns::ServiceInstancePublicationResponderPtr responder,
    PublishServiceInstanceCallback callback)
    : responder_(std::move(responder)), callback_(std::move(callback)) {
  FX_DCHECK(responder_);
  FX_DCHECK(callback_);

  // This handler ensures that |Quit| will be called when the responder channel is closed. We don't
  // unbind from this end or remove the handler, except in the destructor. |Quit| causes this
  // publisher to be deleted, so the lifetime of the publisher is effectively scoped to the lifetime
  // of the channel.
  responder_.set_error_handler([this](zx_status_t status) mutable { Quit(); });

  responder_.events().SetSubtypes = [this](std::vector<std::string> subtypes) {
    for (const auto& subtype : subtypes) {
      if (!MdnsNames::IsValidSubtypeName(subtype)) {
        FX_LOGS(ERROR) << "Invalid subtype " << subtype
                       << " passed in SetSubtypes event, closing connection.";
        responder_ = nullptr;
        Quit();
        return;
      }
    }

    SetSubtypes(std::move(subtypes));
  };

  responder_.events().Reannounce = [this]() { Reannounce(); };
}

ServiceInstancePublisherServiceImpl::ResponderPublisher::~ResponderPublisher() {
  responder_.set_error_handler(nullptr);

  if (responder_.is_bound()) {
    responder_.Unbind();
  }
}

void ServiceInstancePublisherServiceImpl::ResponderPublisher::Quit() {
  // This method is called by the error handler for the responder channel, ensuring that this
  // publisher will be destroyed shortly after the channel is closed from the other end.
  delete this;
}

void ServiceInstancePublisherServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  FX_DCHECK(callback_);

  if (success) {
    callback_(fpromise::ok());
  } else {
    callback_(fpromise::error(
        fuchsia::net::mdns::PublishServiceInstanceError::ALREADY_PUBLISHED_ON_SUBNET));
  }
}

void ServiceInstancePublisherServiceImpl::ResponderPublisher::GetPublication(
    PublicationCause publication_cause, const std::string& subtype,
    const std::vector<inet::SocketAddress>& source_addresses, GetPublicationCallback callback) {
  if (on_publication_calls_in_progress_ < kMaxOnPublicationCallsInProgress) {
    ++on_publication_calls_in_progress_;
    GetPublicationNow(publication_cause, subtype, source_addresses, std::move(callback));
  } else {
    pending_publications_.emplace(publication_cause, subtype, source_addresses,
                                  std::move(callback));
  }
}

void ServiceInstancePublisherServiceImpl::ResponderPublisher::OnGetPublicationComplete() {
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

void ServiceInstancePublisherServiceImpl::ResponderPublisher::GetPublicationNow(
    PublicationCause publication_cause, std::string subtype,
    std::vector<inet::SocketAddress> source_addresses, GetPublicationCallback callback) {
  FX_DCHECK(subtype.empty() || MdnsNames::IsValidSubtypeName(subtype));

  FX_DCHECK(responder_);
  responder_->OnPublication(
      fidl::To<fuchsia::net::mdns::ServiceInstancePublicationCause>(publication_cause),
      subtype.empty() ? fidl::StringPtr() : fidl::StringPtr(std::move(subtype)),
      fidl::To<std::vector<fuchsia::net::IpAddress>>(source_addresses),
      [this, callback = std::move(callback)](
          fuchsia::net::mdns::ServiceInstancePublicationResponder_OnPublication_Result result) {
        if (result.is_err()) {
          return;
        }

        auto converted =
            fidl::To<std::unique_ptr<Mdns::Publication>>(result.response().publication);
        if (!converted) {
          Quit();
          return;
        }

        callback(std::move(converted));
        OnGetPublicationComplete();
      });
}

}  // namespace mdns
