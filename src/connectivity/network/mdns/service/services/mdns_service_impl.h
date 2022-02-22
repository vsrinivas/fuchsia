// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include "src/connectivity/network/mdns/service/config.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/mdns_deprecated_service_impl.h"
#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"

namespace mdns {

class MdnsServiceImpl : public fuchsia::net::mdns::ServiceInstanceResolver {
 public:
  MdnsServiceImpl(sys::ComponentContext* component_context);

  ~MdnsServiceImpl() override;

  // fuchsia::net:mdns::ServiceInstanceResolver implementation.
  void ResolveServiceInstance(std::string service, std::string instance, int64_t timeout,
                              ResolveServiceInstanceCallback callback) override;

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

  // Like |fidl::BindingSet| but with the ability to pend requests until ready.
  template <typename TProtocol>
  class BindingSet {
   public:
    BindingSet(TProtocol* impl, const std::string& label) : impl_(impl), label_(label) {
      FX_DCHECK(impl_);
    }

    void OnBindRequest(fidl::InterfaceRequest<TProtocol> request) {
      FX_DCHECK(request);
      if (ready_) {
        bindings_.AddBinding(impl_, std::move(request));
      } else {
        pending_requests_.push_back(std::move(request));
      }
    }

    void OnReady() {
      ready_ = true;
      for (auto& request : pending_requests_) {
        bindings_.AddBinding(impl_, std::move(request));
      }

      pending_requests_.clear();
    }

   private:
    TProtocol* impl_;
    std::string label_;
    bool ready_ = false;
    std::vector<fidl::InterfaceRequest<TProtocol>> pending_requests_;
    fidl::BindingSet<TProtocol> bindings_;
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
  BindingSet<fuchsia::net::mdns::ServiceInstanceResolver> service_instance_resolver_bindings_;
  Mdns mdns_;
  MdnsTransceiver transceiver_;
  std::unordered_map<std::string, std::unique_ptr<Mdns::Publisher>>
      publishers_by_instance_full_name_;

  MdnsDeprecatedServiceImpl deprecated_services_;

 public:
  // Disallow copy, assign and move.
  MdnsServiceImpl(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl(MdnsServiceImpl&&) = delete;
  MdnsServiceImpl& operator=(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl& operator=(MdnsServiceImpl&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_SERVICE_IMPL_H_
