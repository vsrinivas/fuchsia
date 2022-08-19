// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_SUBSCRIBER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_SUBSCRIBER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"

namespace mdns {

class HostNameSubscriberServiceImpl
    : public ServiceImplBase<fuchsia::net::mdns::HostNameSubscriber> {
 public:
  HostNameSubscriberServiceImpl(
      Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::HostNameSubscriber> request,
      fit::closure deleter);

  ~HostNameSubscriberServiceImpl() override = default;

  // fuchsia::net::mdns::HostNameSubscriber implementation.
  void SubscribeToHostName(
      std::string host_name, fuchsia::net::mdns::HostNameSubscriptionOptions options,
      fidl::InterfaceHandle<fuchsia::net::mdns::HostNameSubscriptionListener> listener) override;

 private:
  class Subscriber : public Mdns::HostNameSubscriber {
   public:
    Subscriber(fidl::InterfaceHandle<fuchsia::net::mdns::HostNameSubscriptionListener> handle);

    ~Subscriber() override;

    // Mdns::HostNameSubscriber implementation:
    void AddressesChanged(std::vector<HostAddress> addresses) override;

   private:
    static constexpr size_t kMaxPipelineDepth = 16;

    void MaybeSendAddresses();

    fuchsia::net::mdns::HostNameSubscriptionListenerPtr client_;
    std::vector<HostAddress> addresses_;
    bool dirty_ = false;
    size_t pipeline_depth_ = 0;

    // Disallow copy, assign and move.
    Subscriber(const Subscriber&) = delete;
    Subscriber(Subscriber&&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber& operator=(Subscriber&&) = delete;
  };
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_HOST_NAME_SUBSCRIBER_SERVICE_IMPL_H_
