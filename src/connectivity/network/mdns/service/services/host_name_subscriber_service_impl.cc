// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/host_name_subscriber_service_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

HostNameSubscriberServiceImpl::HostNameSubscriberServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::HostNameSubscriber> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::HostNameSubscriber>(mdns, std::move(request),
                                                              std::move(deleter)) {}

void HostNameSubscriberServiceImpl::SubscribeToHostName(
    std::string host_name, fuchsia::net::mdns::HostNameSubscriptionOptions options,
    fidl::InterfaceHandle<fuchsia::net::mdns::HostNameSubscriptionListener> listener_handle) {
  if (!MdnsNames::IsValidHostName(host_name)) {
    FX_LOGS(ERROR) << "SubscribeToHostName called with invalid host name " << host_name
                   << ", closing connection.";
    Quit();
    return;
  }

  Media media = options.has_media() ? fidl::To<Media>(options.media()) : Media::kBoth;
  IpVersions ip_versions =
      options.has_ip_versions() ? fidl::To<IpVersions>(options.ip_versions()) : IpVersions::kBoth;

  size_t id = next_host_name_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(
      std::move(listener_handle), [this, id]() { host_name_subscribers_by_id_.erase(id); });

  bool include_local = !options.has_exclude_local() || !options.exclude_local();
  bool include_local_proxies =
      !options.has_exclude_local_proxies() || !options.exclude_local_proxies();

  mdns().SubscribeToHostName(host_name, media, ip_versions, include_local, include_local_proxies,
                             subscriber.get());
}

////////////////////////////////////////////////////////////////////////////////
// HostNameSubscriberServiceImpl::Subscriber implementation

HostNameSubscriberServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceHandle<fuchsia::net::mdns::HostNameSubscriptionListener> handle,
    fit::closure deleter) {
  client_.Bind(std::move(handle));
  client_.set_error_handler([this, deleter = std::move(deleter)](zx_status_t status) mutable {
    // Clearing the error handler frees the capture list, so we need to save |deleter|.
    auto save_deleter = std::move(deleter);
    client_.set_error_handler(nullptr);
    client_.Unbind();
    save_deleter();
  });
}

HostNameSubscriberServiceImpl::Subscriber::~Subscriber() {
  client_.set_error_handler(nullptr);
  if (client_.is_bound()) {
    client_.Unbind();
  }
}

void HostNameSubscriberServiceImpl::Subscriber::AddressesChanged(
    std::vector<HostAddress> addresses) {
  addresses_ = std::move(addresses);
  dirty_ = true;
  MaybeSendAddresses();
}

void HostNameSubscriberServiceImpl::Subscriber::MaybeSendAddresses() {
  FX_DCHECK(pipeline_depth_ <= kMaxPipelineDepth);
  if (!dirty_ || pipeline_depth_ == kMaxPipelineDepth) {
    return;
  }

  client_->OnAddressesChanged(fidl::To<std::vector<fuchsia::net::mdns::HostAddress>>(addresses_),
                              [this]() {
                                FX_DCHECK(pipeline_depth_ != 0);
                                --pipeline_depth_;
                                MaybeSendAddresses();
                              });

  ++pipeline_depth_;
  dirty_ = false;
}

}  // namespace mdns
