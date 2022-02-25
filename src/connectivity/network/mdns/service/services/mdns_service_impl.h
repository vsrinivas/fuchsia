// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include "src/connectivity/network/mdns/service/config.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/mdns_deprecated_service_impl.h"
#include "src/connectivity/network/mdns/service/services/service_impl_manager.h"
#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"

namespace mdns {

// An instance of this class handles all connect requests for discoverable mdns services. None of
// those services are implemented here, because they all require 'nonce' implementations that each
// serve just one client. This calls also reads and implements the service configuration file.
class MdnsServiceImpl {
 public:
  MdnsServiceImpl(sys::ComponentContext* component_context);

  ~MdnsServiceImpl();

 private:
  // Publisher for PublishServiceInstance.
  class SimplePublisher : public Mdns::Publisher {
   public:
    SimplePublisher(
        std::unique_ptr<Mdns::Publication> publication,
        fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback);

   private:
    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(Mdns::PublicationCause publication_cause, const std::string& subtype,
                        const std::vector<inet::SocketAddress>& source_addresses,
                        GetPublicationCallback callback) override;

    std::unique_ptr<Mdns::Publication> publication_;
    fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback_;

    // Disallow copy, assign and move.
    SimplePublisher(const SimplePublisher&) = delete;
    SimplePublisher(SimplePublisher&&) = delete;
    SimplePublisher& operator=(const SimplePublisher&) = delete;
    SimplePublisher& operator=(SimplePublisher&&) = delete;
  };

  // Starts the service.
  void Start();

  // Handles the ready callback from |mdns_|.
  void OnReady();

  // Publishes a service instance using |SimplePublisher|.
  bool PublishServiceInstance(
      std::string service_name, std::string instance_name,
      std::unique_ptr<Mdns::Publication> publication, bool perform_probe, Media media,
      fit::function<void(fpromise::result<void, fuchsia::net::mdns::Error>)> callback);

  sys::ComponentContext* component_context_;
  Config config_;
  bool ready_ = false;
  Mdns mdns_;
  MdnsTransceiver transceiver_;
  std::unordered_map<std::string, std::unique_ptr<Mdns::Publisher>>
      publishers_by_instance_full_name_;

  MdnsDeprecatedServiceImpl deprecated_services_;

  ServiceImplManager<fuchsia::net::mdns::ServiceInstanceResolver>
      service_instance_resolver_manager_;

 public:
  // Disallow copy, assign and move.
  MdnsServiceImpl(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl(MdnsServiceImpl&&) = delete;
  MdnsServiceImpl& operator=(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl& operator=(MdnsServiceImpl&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_
