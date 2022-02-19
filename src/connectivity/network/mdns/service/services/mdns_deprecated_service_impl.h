// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_DEPRECATED_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_DEPRECATED_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"

namespace mdns {

class MdnsDeprecatedServiceImpl : public fuchsia::net::mdns::Resolver,
                                  public fuchsia::net::mdns::Subscriber,
                                  public fuchsia::net::mdns::Publisher {
 public:
  MdnsDeprecatedServiceImpl(Mdns& mdns, sys::ComponentContext* component_context);

  ~MdnsDeprecatedServiceImpl() override;

  // Completes deferred bindings.
  void OnReady();

  // fuchsia::net::mdns::Resolver implementation.
  void ResolveHostName(std::string host_name, int64_t timeout_ns,
                       ResolveHostNameCallback callback) override;

  // fuchsia::net::mdns::Subscriber implementation.
  void SubscribeToService(
      std::string service,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber) override;

  // fuchsia::net::mdns::Publisher implementation.
  void PublishServiceInstance(
      std::string service, std::string instance, fuchsia::net::mdns::Media media,
      bool perform_probe,
      fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle,
      PublishServiceInstanceCallback callback) override;

  class Subscriber : public Mdns::Subscriber {
   public:
    Subscriber(fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> handle,
               fit::closure deleter);

    ~Subscriber() override;

    // Mdns::Subscriber implementation:
    void InstanceDiscovered(const std::string& service, const std::string& instance,
                            const inet::SocketAddress& v4_address,
                            const inet::SocketAddress& v6_address,
                            const std::vector<std::string>& text, uint16_t srv_priority,
                            uint16_t srv_weight, const std::string& target) override;

    void InstanceChanged(const std::string& service, const std::string& instance,
                         const inet::SocketAddress& v4_address,
                         const inet::SocketAddress& v6_address,
                         const std::vector<std::string>& text, uint16_t srv_priority,
                         uint16_t srv_weight, const std::string& target) override;

    void InstanceLost(const std::string& service, const std::string& instance) override;

    void Query(DnsType type_queried) override;

   private:
    static constexpr size_t kMaxPipelineDepth = 16;

    enum class EntryType {
      kInstanceDiscovered,
      kInstanceChanged,
      kInstanceLost,
      kQuery,
    };

    struct Entry {
      EntryType type;
      fuchsia::net::mdns::ServiceInstance service_instance;
      DnsType type_queried;
    };

    // Sends the entry at the head of the queue, if there is one and if
    // |pipeline_depth_| is less than |kMaxPipelineDepth|.
    void MaybeSendNextEntry();

    // Decrements |pipeline_depth_| and calls |MaybeSendNextEntry|.
    void ReplyReceived();

    fuchsia::net::mdns::ServiceSubscriberPtr client_;
    std::queue<Entry> entries_;
    size_t pipeline_depth_ = 0;

    // Disallow copy, assign and move.
    Subscriber(const Subscriber&) = delete;
    Subscriber(Subscriber&&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber& operator=(Subscriber&&) = delete;
  };

  // Publisher for AddResponder.
  class ResponderPublisher : public Mdns::Publisher {
   public:
    ResponderPublisher(fuchsia::net::mdns::PublicationResponderPtr responder,
                       PublishServiceInstanceCallback callback, fit::closure deleter);

    ~ResponderPublisher() override;

    // Disallow copy, assign and move.
    ResponderPublisher(const ResponderPublisher&) = delete;
    ResponderPublisher(ResponderPublisher&&) = delete;
    ResponderPublisher& operator=(const ResponderPublisher&) = delete;
    ResponderPublisher& operator=(ResponderPublisher&&) = delete;

    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(Mdns::PublicationCause publication_cause, const std::string& subtype,
                        const std::vector<inet::SocketAddress>& source_addresses,
                        GetPublicationCallback callback) override;

   private:
    // The maximum number of |OnPublication| method calls that may be in progress at any one time.
    // This limit is set to prevent the responder channel for overflowing.
    static constexpr uint32_t kMaxOnPublicationCallsInProgress = 2;

    struct Entry {
      Entry(Mdns::PublicationCause publication_cause, const std::string& subtype,
            const std::vector<inet::SocketAddress>& source_addresses,
            GetPublicationCallback callback)
          : publication_cause_(publication_cause),
            subtype_(subtype),
            source_addresses_(source_addresses),
            callback_(std::move(callback)) {}
      Mdns::PublicationCause publication_cause_;
      std::string subtype_;
      std::vector<inet::SocketAddress> source_addresses_;
      GetPublicationCallback callback_;
    };

    // Handles completion of an |OnPublication| call.
    void OnGetPublicationComplete();

    // Calls the responder's |OnPublication| method and, if all goes well, calls the callback and
    // |OnGetPublicationComplete|. If the response to |OnPublication| is malformed, this method
    // calls |Unpublish| instead.
    void GetPublicationNow(Mdns::PublicationCause publication_cause, const std::string& subtype,
                           const std::vector<inet::SocketAddress>& source_addresses,
                           GetPublicationCallback callback);

    fuchsia::net::mdns::PublicationResponderPtr responder_;
    PublishServiceInstanceCallback callback_;
    std::queue<Entry> pending_publications_;
    uint32_t on_publication_calls_in_progress_ = 0;
  };

 private:
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

  BindingSet<fuchsia::net::mdns::Resolver> resolver_bindings_;
  BindingSet<fuchsia::net::mdns::Subscriber> subscriber_bindings_;
  BindingSet<fuchsia::net::mdns::Publisher> publisher_bindings_;
  Mdns& mdns_;
  size_t next_subscriber_id_ = 0;
  std::unordered_map<size_t, std::unique_ptr<Subscriber>> subscribers_by_id_;
  std::unordered_map<std::string, std::unique_ptr<Mdns::Publisher>>
      publishers_by_instance_full_name_;

 public:
  // Disallow copy, assign and move.
  MdnsDeprecatedServiceImpl(const MdnsDeprecatedServiceImpl&) = delete;
  MdnsDeprecatedServiceImpl(MdnsDeprecatedServiceImpl&&) = delete;
  MdnsDeprecatedServiceImpl& operator=(const MdnsDeprecatedServiceImpl&) = delete;
  MdnsDeprecatedServiceImpl& operator=(MdnsDeprecatedServiceImpl&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_MDNS_DEPRECATED_SERVICE_IMPL_H_
