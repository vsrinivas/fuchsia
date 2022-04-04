// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/proxy_host_publisher_service_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

ProxyHostPublisherServiceImpl::ProxyHostPublisherServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ProxyHostPublisher> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::ProxyHostPublisher>(mdns, std::move(request),
                                                              std::move(deleter)) {}

void ProxyHostPublisherServiceImpl::PublishProxyHost(
    std::string host_name, std::vector<fuchsia::net::IpAddress> addresses,
    fuchsia::net::mdns::ProxyHostPublicationOptions options,
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
    PublishProxyHostCallback callback) {
  if (!MdnsNames::IsValidHostName(host_name)) {
    FX_LOGS(ERROR) << "PublishProxyHost called with invalid host name " << host_name
                   << ", closing connection.";
    Quit(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (addresses.empty()) {
    FX_LOGS(ERROR) << "PublishProxyHost called with empty address list, closing connection.";
    Quit(ZX_ERR_INVALID_ARGS);
    return;
  }

  Media media = options.has_media() ? fidl::To<Media>(options.media()) : Media::kBoth;
  IpVersions ip_versions =
      options.has_ip_versions() ? fidl::To<IpVersions>(options.ip_versions()) : IpVersions::kBoth;
  bool perform_probe = options.has_perform_probe() ? options.perform_probe() : true;

  std::vector<inet::IpAddress> inet_addresses;
  std::transform(
      addresses.begin(), addresses.end(), std::back_inserter(inet_addresses),
      [](const fuchsia::net::IpAddress& fidl_address) { return inet::IpAddress(fidl_address); });

  host_publisher_ = std::make_unique<HostPublisher>();

  if (!mdns().PublishHost(host_name, inet_addresses, media, ip_versions, perform_probe,
                          host_publisher_.get())) {
    callback(fpromise::error(fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_LOCALLY));
    return;
  }

  host_publisher_->WhenSuccessReported([this, host_name = std::move(host_name),
                                        addresses = std::move(inet_addresses), media, ip_versions,
                                        request = std::move(request),
                                        callback = std::move(callback)](bool success) mutable {
    if (!success) {
      callback(
          fpromise::error(fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_ON_SUBNET));
      return;
    }

    // Create a ServiceInstancePublisher for the new host.
    instance_publisher_service_ = std::make_unique<ServiceInstancePublisherServiceImpl>(
        mdns(), std::move(host_name), std::move(addresses), media, ip_versions, std::move(request),
        [this]() { instance_publisher_service_ = nullptr; });
  });
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyHostPublisherServiceImpl::HostPublisher definitions

void ProxyHostPublisherServiceImpl::HostPublisher::WhenSuccessReported(
    fit::function<void(bool)> callback) {
  if (success_.has_value()) {
    callback(success_.value());
  } else {
    callback_ = std::move(callback);
  }
}

void ProxyHostPublisherServiceImpl::HostPublisher::ReportSuccess(bool success) {
  success_ = success;

  if (callback_) {
    callback_(success);
    callback_ = nullptr;
  }
}

}  // namespace mdns
