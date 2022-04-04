// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_PUBLISHER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_PUBLISHER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"

namespace mdns {

class ServiceInstancePublisherServiceImpl
    : public ServiceImplBase<fuchsia::net::mdns::ServiceInstancePublisher> {
 public:
  ServiceInstancePublisherServiceImpl(
      Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
      fit::closure deleter);

  ServiceInstancePublisherServiceImpl(
      Mdns& mdns, std::string host_name, std::vector<inet::IpAddress> addresses,
      Media default_media, IpVersions default_ip_versions,
      fidl::InterfaceRequest<fuchsia::net::mdns::ServiceInstancePublisher> request,
      fit::closure deleter);

  ~ServiceInstancePublisherServiceImpl() override = default;

  // fuchsia::net:mdns::ServiceInstancePublisher implementation.
  void PublishServiceInstance(
      std::string service, std::string instance,
      fuchsia::net::mdns::ServiceInstancePublicationOptions options,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceInstancePublicationResponder>
          publication_responder,
      PublishServiceInstanceCallback callback) override;

 private:
  // Publisher for PublishServiceInstance.
  class ResponderPublisher : public Mdns::Publisher {
   public:
    ResponderPublisher(fuchsia::net::mdns::ServiceInstancePublicationResponderPtr responder,
                       PublishServiceInstanceCallback callback, fit::closure deleter);

    ~ResponderPublisher() override;

    // Disallow copy, assign and move.
    ResponderPublisher(const ResponderPublisher&) = delete;
    ResponderPublisher(ResponderPublisher&&) = delete;
    ResponderPublisher& operator=(const ResponderPublisher&) = delete;
    ResponderPublisher& operator=(ResponderPublisher&&) = delete;

    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(PublicationCause publication_cause, const std::string& subtype,
                        const std::vector<inet::SocketAddress>& source_addresses,
                        GetPublicationCallback callback) override;

   private:
    // The maximum number of |OnPublication| method calls that may be in progress at any one time.
    // This limit is set to prevent the responder channel from overflowing.
    static constexpr uint32_t kMaxOnPublicationCallsInProgress = 2;

    struct Entry {
      Entry(PublicationCause publication_cause, std::string subtype,
            std::vector<inet::SocketAddress> source_addresses, GetPublicationCallback callback)
          : publication_cause_(publication_cause),
            subtype_(std::move(subtype)),
            source_addresses_(std::move(source_addresses)),
            callback_(std::move(callback)) {}
      PublicationCause publication_cause_;
      std::string subtype_;
      std::vector<inet::SocketAddress> source_addresses_;
      GetPublicationCallback callback_;
    };

    void Quit();

    // Handles completion of an |OnPublication| call.
    void OnGetPublicationComplete();

    // Calls the responder's |OnPublication| method and, if all goes well, calls the callback and
    // |OnGetPublicationComplete|. If the response to |OnPublication| is malformed, this method
    // calls |Unpublish| instead.
    void GetPublicationNow(PublicationCause publication_cause, std::string subtype,
                           std::vector<inet::SocketAddress> source_addresses,
                           GetPublicationCallback callback);

    fuchsia::net::mdns::ServiceInstancePublicationResponderPtr responder_;
    PublishServiceInstanceCallback callback_;
    fit::closure deleter_;
    std::queue<Entry> pending_publications_;
    uint32_t on_publication_calls_in_progress_ = 0;
  };

  std::string host_name_;
  std::vector<inet::IpAddress> addresses_;
  Media default_media_;
  IpVersions default_ip_versions_;
  std::unordered_map<uint64_t, std::unique_ptr<Mdns::Publisher>> publishers_by_id_;
  uint64_t next_publisher_id_ = 0;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_INSTANCE_PUBLISHER_SERVICE_IMPL_H_
