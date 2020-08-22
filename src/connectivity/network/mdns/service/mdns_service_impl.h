// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include "src/connectivity/network/mdns/service/config.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/mdns_transceiver.h"

namespace mdns {

class MdnsServiceImpl : public fuchsia::net::mdns::Resolver,
                        public fuchsia::net::mdns::Subscriber,
                        public fuchsia::net::mdns::Publisher {
 public:
  MdnsServiceImpl(sys::ComponentContext* component_context);

  ~MdnsServiceImpl() override;

  // fuchsia::net::mdns::Resolver implementation.
  void ResolveHostName(std::string host_name, int64_t timeout_ns,
                       ResolveHostNameCallback callback) override;

  // fuchsia::net::mdns::Subscriber implementation.
  void SubscribeToService(
      std::string service,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> subscriber) override;

  void SubscribeToService2(
      std::string service,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber2> subscriber) override;

  // fuchsia::net::mdns::Publisher implementation.
  void PublishServiceInstance(
      std::string service, std::string instance, bool perform_probe,
      fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder> responder_handle,
      PublishServiceInstanceCallback callback) override;

  void PublishServiceInstance2(
      std::string service, std::string instance, bool perform_probe,
      fidl::InterfaceHandle<fuchsia::net::mdns::PublicationResponder2> responder_handle,
      PublishServiceInstance2Callback callback) override;

 private:
  class Subscriber : public Mdns::Subscriber {
   public:
    Subscriber(fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber> handle,
               fit::closure deleter);

    Subscriber(fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriber2> handle,
               fit::closure deleter);

    ~Subscriber() override;

    // Mdns::Subscriber implementation:
    void InstanceDiscovered(const std::string& service, const std::string& instance,
                            const inet::SocketAddress& v4_address,
                            const inet::SocketAddress& v6_address,
                            const std::vector<std::string>& text, uint16_t srv_priority,
                            uint16_t srv_weight) override;

    void InstanceChanged(const std::string& service, const std::string& instance,
                         const inet::SocketAddress& v4_address,
                         const inet::SocketAddress& v6_address,
                         const std::vector<std::string>& text, uint16_t srv_priority,
                         uint16_t srv_weight) override;

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
    fuchsia::net::mdns::ServiceSubscriber2Ptr client2_;
    std::queue<Entry> entries_;
    size_t pipeline_depth_ = 0;

    // Disallow copy, assign and move.
    Subscriber(const Subscriber&) = delete;
    Subscriber(Subscriber&&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber& operator=(Subscriber&&) = delete;
  };

  // Publisher for PublishServiceInstance.
  class SimplePublisher : public Mdns::Publisher {
   public:
    SimplePublisher(std::unique_ptr<Mdns::Publication> publication,
                    PublishServiceInstanceCallback callback);

   private:
    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(bool query, const std::string& subtype,
                        const std::vector<inet::SocketAddress>& source_addresses,
                        fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override;

    std::unique_ptr<Mdns::Publication> publication_;
    PublishServiceInstanceCallback callback_;

    // Disallow copy, assign and move.
    SimplePublisher(const SimplePublisher&) = delete;
    SimplePublisher(SimplePublisher&&) = delete;
    SimplePublisher& operator=(const SimplePublisher&) = delete;
    SimplePublisher& operator=(SimplePublisher&&) = delete;
  };

  // Publisher for AddResponder.
  class ResponderPublisher : public Mdns::Publisher {
   public:
    ResponderPublisher(fuchsia::net::mdns::PublicationResponderPtr responder,
                       PublishServiceInstanceCallback callback, fit::closure deleter);

    ResponderPublisher(fuchsia::net::mdns::PublicationResponder2Ptr responder,
                       PublishServiceInstance2Callback callback, fit::closure deleter);

    ~ResponderPublisher() override;

    // Mdns::Publisher implementation.
    void ReportSuccess(bool success) override;

    void GetPublication(bool query, const std::string& subtype,
                        const std::vector<inet::SocketAddress>& source_addresses,
                        fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override;

    fuchsia::net::mdns::PublicationResponderPtr responder_;
    PublishServiceInstanceCallback callback_;
    fuchsia::net::mdns::PublicationResponder2Ptr responder2_;
    PublishServiceInstance2Callback callback2_;

    // Disallow copy, assign and move.
    ResponderPublisher(const ResponderPublisher&) = delete;
    ResponderPublisher(ResponderPublisher&&) = delete;
    ResponderPublisher& operator=(const ResponderPublisher&) = delete;
    ResponderPublisher& operator=(ResponderPublisher&&) = delete;
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
  bool PublishServiceInstance(std::string service_name, std::string instance_name,
                              std::unique_ptr<Mdns::Publication> publication, bool perform_probe,
                              Media media, PublishServiceInstanceCallback callback);

  sys::ComponentContext* component_context_;
  Config config_;
  bool ready_ = false;
  BindingSet<fuchsia::net::mdns::Resolver> resolver_bindings_;
  BindingSet<fuchsia::net::mdns::Subscriber> subscriber_bindings_;
  BindingSet<fuchsia::net::mdns::Publisher> publisher_bindings_;
  Mdns mdns_;
  MdnsTransceiver transceiver_;
  size_t next_subscriber_id_ = 0;
  std::unordered_map<size_t, std::unique_ptr<Subscriber>> subscribers_by_id_;
  std::unordered_map<std::string, std::unique_ptr<Mdns::Publisher>>
      publishers_by_instance_full_name_;

 public:
  // Disallow copy, assign and move.
  MdnsServiceImpl(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl(MdnsServiceImpl&&) = delete;
  MdnsServiceImpl& operator=(const MdnsServiceImpl&) = delete;
  MdnsServiceImpl& operator=(MdnsServiceImpl&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_SERVICE_IMPL_H_
