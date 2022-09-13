// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_SUBSCRIBER_SERVICE_IMPL_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_SUBSCRIBER_SERVICE_IMPL_H_

#include <fuchsia/net/mdns/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/services/service_impl_base.h"

namespace mdns {

class ServiceSubscriberServiceImpl
    : public ServiceImplBase<fuchsia::net::mdns::ServiceSubscriber2> {
 public:
  ServiceSubscriberServiceImpl(
      Mdns& mdns, fidl::InterfaceRequest<fuchsia::net::mdns::ServiceSubscriber2> request,
      fit::closure deleter);

  ~ServiceSubscriberServiceImpl() override = default;

  // fuchsia::net::mdns::ServiceSubscriber2 implementation.
  void SubscribeToService(
      std::string service, fuchsia::net::mdns::ServiceSubscriptionOptions options,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> listener) override;

  void SubscribeToAllServices(
      fuchsia::net::mdns::ServiceSubscriptionOptions options,
      fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> listener) override;

 private:
  class Subscriber : public Mdns::Subscriber {
   public:
    Subscriber(fidl::InterfaceHandle<fuchsia::net::mdns::ServiceSubscriptionListener> handle,
               fit::closure deleter);

    ~Subscriber() override;

    // Mdns::Subscriber implementation:
    void InstanceDiscovered(const std::string& service, const std::string& instance,
                            const std::vector<inet::SocketAddress>& addresses,
                            const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
                            uint16_t srv_weight, const std::string& target) override;

    void InstanceChanged(const std::string& service, const std::string& instance,
                         const std::vector<inet::SocketAddress>& addresses,
                         const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
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

    // Prevents a call to |MaybeDelete| from calling the deleter by incrementing
    // |one_based_delete_counter_|.
    void DeferDeletion();

    // Decrements |one_based_delete_counter_| and, if the resulting value is zero, calls the
    // deleter.
    void MaybeDelete();

    fuchsia::net::mdns::ServiceSubscriptionListenerPtr client_;
    std::queue<Entry> entries_;
    size_t pipeline_depth_ = 0;
    size_t one_based_delete_counter_ = 1;
    fit::closure deleter_;

    // Disallow copy, assign and move.
    Subscriber(const Subscriber&) = delete;
    Subscriber(Subscriber&&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber& operator=(Subscriber&&) = delete;
  };

  size_t next_service_instance_subscriber_id_ = 0;
  std::unordered_map<size_t, std::unique_ptr<Subscriber>> service_instance_subscribers_by_id_;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_SERVICES_SERVICE_SUBSCRIBER_SERVICE_IMPL_H_
