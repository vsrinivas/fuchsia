// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MDNS_CPP_SERVICE_SUBSCRIBER_H_
#define LIB_MDNS_CPP_SERVICE_SUBSCRIBER_H_

#include <fuchsia/mdns/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"

namespace mdns {

// Manages a subscription to an mDNS service type.
class ServiceSubscriber {
 public:
  // Callback type used to notify of service changes. When a new service is
  // discovered, the callback is called with a null |from| value and |to| value
  // describing the new service. When a service is lost, the callback is called
  // with a null |to| value and |from| value describing the lost service. When
  // a service changes, |from| is the old description, and |to| is the new one.
  using UpdateCallback =
      fit::function<void(const fuchsia::mdns::MdnsServiceInstance* from,
                         const fuchsia::mdns::MdnsServiceInstance* to)>;

  ServiceSubscriber();

  ~ServiceSubscriber();

  // Initializes the subscriber with the specified subscription. The callback
  // is optional.
  void Init(fuchsia::mdns::MdnsServiceSubscriptionPtr subscription,
            UpdateCallback callback);

  // Returns this subscriber to its initial state, releasing the callback and
  // returning the unique subscription. The subscription can be ignored, in
  // which case it will be closed.
  fuchsia::mdns::MdnsServiceSubscriptionPtr Reset();

  // Returns the current set of service instances.
  const fidl::VectorPtr<fuchsia::mdns::MdnsServiceInstance>& instances() const {
    return instances_;
  }

  // Returns the subscription.
  const fuchsia::mdns::MdnsServiceSubscriptionPtr& subscription() const {
    return subscription_;
  }

 private:
  void HandleInstanceUpdates(
      uint64_t version = fuchsia::mdns::kInitialInstances,
      fidl::VectorPtr<fuchsia::mdns::MdnsServiceInstance> instances = nullptr);

  void IssueCallbacks(
      const std::vector<fuchsia::mdns::MdnsServiceInstance>& instances);

  fuchsia::mdns::MdnsServiceSubscriptionPtr subscription_;
  UpdateCallback callback_;
  fidl::VectorPtr<fuchsia::mdns::MdnsServiceInstance> instances_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceSubscriber);
};

}  // namespace mdns

#endif  // LIB_MDNS_CPP_SERVICE_SUBSCRIBER_H_
