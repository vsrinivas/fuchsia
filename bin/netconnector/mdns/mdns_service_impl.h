// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/util/fidl_publisher.h"
#include "apps/netconnector/services/mdns.fidl.h"
#include "apps/netconnector/src/mdns/mdns.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace netconnector {
namespace mdns {

class MdnsServiceImpl : public MdnsService {
 public:
  MdnsServiceImpl();

  ~MdnsServiceImpl() override;

  void AddBinding(fidl::InterfaceRequest<MdnsService> request);

  void Start(const std::string& host_name);

  // Registers interest in the specified service.
  void SubscribeToService(const std::string& service_name,
                          const Mdns::ServiceInstanceCallback& callback);

  // Starts publishing the indicated service instance.
  void PublishServiceInstance(const std::string& service_name,
                              const std::string& instance_name,
                              IpPort port,
                              const std::vector<std::string>& text);

  // MdnsService implementation.
  void ResolveHostName(const fidl::String& host_name,
                       uint32_t timeout_ms,
                       const ResolveHostNameCallback& callback) override;

  void SubscribeToService(const fidl::String& service_name,
                          fidl::InterfaceRequest<MdnsServiceSubscription>
                              subscription_request) override;

  void PublishServiceInstance(const fidl::String& service_name,
                              const fidl::String& instance_name,
                              uint16_t port,
                              fidl::Array<fidl::String> text) override;

  void UnpublishServiceInstance(const fidl::String& service_name,
                                const fidl::String& instance_name) override;

  void SetVerbose(bool value) override;

 private:
  class MdnsServiceSubscriptionImpl : public MdnsServiceSubscription {
   public:
    MdnsServiceSubscriptionImpl(MdnsServiceImpl* owner,
                                const std::string& service_name);

    ~MdnsServiceSubscriptionImpl() override;

    void AddBinding(
        fidl::InterfaceRequest<MdnsServiceSubscription> subscription_request);

    // Sets a callback for a in-proc party. This is used by |NetConnectorImpl|
    // to discover Fuchsia devices.
    void SetCallback(const Mdns::ServiceInstanceCallback& callback) {
      callback_ = callback;
    }

    // MdnsServiceSubscription implementation.
    void GetInstances(uint64_t version_last_seen,
                      const GetInstancesCallback& callback) override;

   private:
    MdnsServiceImpl* owner_;
    fidl::BindingSet<MdnsServiceSubscription> bindings_;
    Mdns::ServiceInstanceCallback callback_;
    media::FidlPublisher<GetInstancesCallback> instances_publisher_;
    std::unordered_map<std::string, MdnsServiceInstancePtr> instances_by_name_;

    FTL_DISALLOW_COPY_AND_ASSIGN(MdnsServiceSubscriptionImpl);
  };

  fidl::BindingSet<MdnsService> bindings_;
  mdns::Mdns mdns_;
  std::unordered_map<std::string, std::unique_ptr<MdnsServiceSubscriptionImpl>>
      subscriptions_by_service_name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MdnsServiceImpl);
};

}  // namespace mdns
}  // namespace netconnector
