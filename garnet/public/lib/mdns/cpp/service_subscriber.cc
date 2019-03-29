// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mdns/cpp/service_subscriber.h"

#include "src/lib/fxl/logging.h"

namespace mdns {

ServiceSubscriber::ServiceSubscriber() {}

ServiceSubscriber::~ServiceSubscriber() { Reset(); }

void ServiceSubscriber::Init(fuchsia::mdns::ServiceSubscriptionPtr subscription,
                             UpdateCallback callback) {
  subscription_ = std::move(subscription);
  callback_ = std::move(callback);
  HandleInstanceUpdates();
}

fuchsia::mdns::ServiceSubscriptionPtr ServiceSubscriber::Reset() {
  callback_ = nullptr;
  instances_.reset();
  return std::move(subscription_);
}

void ServiceSubscriber::HandleInstanceUpdates(
    uint64_t version,
    fidl::VectorPtr<fuchsia::mdns::ServiceInstance> instances) {
  FXL_DCHECK(subscription_);

  if (instances) {
    if (callback_) {
      IssueCallbacks(instances);
    }

    instances_ = std::move(instances);
  }

  subscription_->GetInstances(
      version, [this](uint64_t version,
                      std::vector<fuchsia::mdns::ServiceInstance> instances) {
        HandleInstanceUpdates(version, fidl::VectorPtr(std::move(instances)));
      });
}

void ServiceSubscriber::IssueCallbacks(
    const std::vector<fuchsia::mdns::ServiceInstance>& instances) {
  // For each instance in the update, see if it represents a new instance or
  // a change with respect to an old instance.
  for (auto& new_instance : instances) {
    bool found = false;

    // Search the old instances to see if there's a match.
    for (auto& old_instance : *instances_) {
      if (new_instance.service_name == old_instance.service_name &&
          new_instance.instance_name == old_instance.instance_name) {
        // Found a match. If there's been a change, issue a callback to
        // indicate that.
        if (new_instance.v4_address != old_instance.v4_address ||
            new_instance.v6_address != old_instance.v6_address ||
            *new_instance.text != *old_instance.text) {
          callback_(&old_instance, &new_instance);
        }

        found = true;
        break;
      }
    }

    if (!found) {
      // No match was found. Issue a callback indicating a new instance.
      callback_(nullptr, &new_instance);
    }
  }

  // For each old instance, determine whether it has been removed.
  for (auto& old_instance : *instances_) {
    bool found = false;

    // Search the new instances to see if there's a match.
    for (auto& new_instance : instances) {
      if (new_instance.service_name == old_instance.service_name &&
          new_instance.instance_name == old_instance.instance_name) {
        found = true;
        break;
      }
    }

    if (!found) {
      // No match was found. Issue a callback indicating a removed instance.
      callback_(&old_instance, nullptr);
    }
  }
}

}  // namespace mdns
