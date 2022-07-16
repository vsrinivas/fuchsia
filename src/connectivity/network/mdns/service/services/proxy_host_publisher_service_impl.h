// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_PROXY_HOST_PUBLISHER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_PROXY_HOST_PUBLISHER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"
#include "src/connectivity/network/mdns/service/services/service_instance_publisher_service_impl.h"

namespace mdns {

class ProxyHostPublisherServiceImpl
    : public ServiceImplBase<fuchsia::net::mdns::ProxyHostPublisher> {
 public:
  ProxyHostPublisherServiceImpl(
      Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ProxyHostPublisher> request,
      fit::closure deleter);

  ~ProxyHostPublisherServiceImpl() override = default;

  // fuchsia::net:mdns::ProxyHostPublisher implementation.
  void PublishProxyHost(std::string host, std::vector<::fuchsia::net::IpAddress> addresses,
                        fuchsia::net::mdns::ProxyHostPublicationOptions options,
                        fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher>
                            service_instance_publisher,
                        PublishProxyHostCallback callback) override;

 private:
  // Publisher for |PublishHost|.
  class HostPublisher : public Mdns::HostPublisher {
   public:
    HostPublisher(PublishProxyHostCallback callback, Mdns& mdns, std::string host_name,
                  std::vector<inet::IpAddress> addresses, Media media, IpVersions ip_versions,
                  fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request);

    ~HostPublisher() override = default;

    // Disallow copy, assign and move.
    HostPublisher(const HostPublisher&) = delete;
    HostPublisher(HostPublisher&&) = delete;
    HostPublisher& operator=(const HostPublisher&) = delete;
    HostPublisher& operator=(HostPublisher&&) = delete;

    // Mdns::HostPublisher implementation.
    void ReportSuccess(bool success) override;

   private:
    PublishProxyHostCallback callback_;
    Mdns& mdns_;
    std::string host_name_;
    std::vector<inet::IpAddress> addresses_;
    Media media_;
    IpVersions ip_versions_;
    fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request_;
    std::unique_ptr<ServiceInstancePublisherServiceImpl> instance_publisher_service_;
  };
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_PROXY_HOST_PUBLISHER_SERVICE_IMPL_H_
