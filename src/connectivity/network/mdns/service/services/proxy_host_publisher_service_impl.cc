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

  auto host_publisher = new HostPublisher(std::move(callback), mdns(), host_name, inet_addresses,
                                          media, ip_versions, std::move(request));

  if (!mdns().PublishHost(host_name, std::move(inet_addresses), media, ip_versions, perform_probe,
                          host_publisher)) {
    callback(fpromise::error(fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_LOCALLY));
    return;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyHostPublisherServiceImpl::HostPublisher definitions

ProxyHostPublisherServiceImpl::HostPublisher::HostPublisher(
    PublishProxyHostCallback callback, Mdns& mdns, std::string host_name,
    std::vector<inet::IpAddress> addresses, Media media, IpVersions ip_versions,
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request)
    : callback_(std::move(callback)),
      mdns_(mdns),
      host_name_(std::move(host_name)),
      addresses_(std::move(addresses)),
      media_(media),
      ip_versions_(ip_versions),
      request_(std::move(request)) {}

void ProxyHostPublisherServiceImpl::HostPublisher::ReportSuccess(bool success) {
  if (!callback_) {
    return;
  }

  if (!success) {
    callback_(
        fpromise::error(fuchsia::net::mdns::PublishProxyHostError::ALREADY_PUBLISHED_ON_SUBNET));
    callback_ = nullptr;
    delete this;
    return;
  }

  instance_publisher_service_ = std::make_unique<ServiceInstancePublisherServiceImpl>(
      mdns_, std::move(host_name_), std::move(addresses_), media_, ip_versions_,
      std::move(request_), [this]() { delete this; });

  callback_(fpromise::ok());
  callback_ = nullptr;
}

}  // namespace mdns
