// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/service_subscriber_service_impl.h"

#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"
#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/reply_address.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"

namespace mdns {

ServiceSubscriberServiceImpl::ServiceSubscriberServiceImpl(
    Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceSubscriber2> request,
    fit::closure deleter)
    : ServiceImplBase<fuchsia::net::mdns::ServiceSubscriber2>(mdns, std::move(request),
                                                              std::move(deleter)) {}

void ServiceSubscriberServiceImpl::SubscribeToService(
    std::string service, fuchsia::net::mdns::ServiceSubscriptionOptions options,
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> listener_handle) {
  if (!MdnsNames::IsValidServiceName(service)) {
    FX_LOGS(ERROR) << "SubscribeToService called with invalid service name " << service
                   << ", closing connection";
    Quit();
    return;
  }

  Media media = options.has_media() ? fidl::To<Media>(options.media()) : Media::kBoth;
  IpVersions ip_versions =
      options.has_ip_versions() ? fidl::To<IpVersions>(options.ip_versions()) : IpVersions::kBoth;

  size_t id = next_service_instance_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(
      std::move(listener_handle), [this, id]() { service_instance_subscribers_by_id_.erase(id); });

  mdns().SubscribeToService(service, media, ip_versions, subscriber.get());

  service_instance_subscribers_by_id_.emplace(id, std::move(subscriber));
}

////////////////////////////////////////////////////////////////////////////////
// ServiceSubscriberServiceImpl::Subscriber implementation

ServiceSubscriberServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> handle,
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

ServiceSubscriberServiceImpl::Subscriber::~Subscriber() {
  client_.set_error_handler(nullptr);
  if (client_.is_bound()) {
    client_.Unbind();
  }
}

void ServiceSubscriberServiceImpl::Subscriber::InstanceDiscovered(
    const std::string& service, const std::string& instance,
    const std::vector<inet::SocketAddress>& addresses,
    const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority, uint16_t srv_weight,
    const std::string& target) {
  Entry entry{.type = EntryType::kInstanceDiscovered};
  MdnsFidlUtil::FillServiceInstance(&entry.service_instance, service, instance, addresses, text,
                                    srv_priority, srv_weight, target);
  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void ServiceSubscriberServiceImpl::Subscriber::InstanceChanged(
    const std::string& service, const std::string& instance,
    const std::vector<inet::SocketAddress>& addresses,
    const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority, uint16_t srv_weight,
    const std::string& target) {
  Entry entry{.type = EntryType::kInstanceChanged};
  MdnsFidlUtil::FillServiceInstance(&entry.service_instance, service, instance, addresses, text,
                                    srv_priority, srv_weight, target);

  entries_.push(std::move(entry));
  MaybeSendNextEntry();
}

void ServiceSubscriberServiceImpl::Subscriber::InstanceLost(const std::string& service,
                                                            const std::string& instance) {
  Entry entry{.type = EntryType::kInstanceLost};
  entry.service_instance.set_service(service);
  entry.service_instance.set_instance(instance);
  entries_.push(std::move(entry));

  MaybeSendNextEntry();
}

void ServiceSubscriberServiceImpl::Subscriber::Query(DnsType type_queried) {
  entries_.push({.type = EntryType::kQuery, .type_queried = type_queried});
  MaybeSendNextEntry();
}

void ServiceSubscriberServiceImpl::Subscriber::MaybeSendNextEntry() {
  FX_DCHECK(pipeline_depth_ <= kMaxPipelineDepth);
  if (pipeline_depth_ == kMaxPipelineDepth || entries_.empty()) {
    return;
  }

  Entry& entry = entries_.front();
  auto on_reply = fit::bind_member<&ServiceSubscriberServiceImpl::Subscriber::ReplyReceived>(this);

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
      client_->OnQuery(fidl::To<fuchsia::net::mdns::ResourceType>(entry.type_queried),
                       std::move(on_reply));
      break;
  }

  ++pipeline_depth_;
  entries_.pop();
}

void ServiceSubscriberServiceImpl::Subscriber::ReplyReceived() {
  FX_DCHECK(pipeline_depth_ != 0);
  --pipeline_depth_;
  MaybeSendNextEntry();
}

}  // namespace mdns
