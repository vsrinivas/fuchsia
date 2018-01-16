// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mdns/cpp/service_subscriber.h"

namespace mdns {
namespace {

template <typename T>
bool operator==(const fidl::Array<T>& array_a, const fidl::Array<T>& array_b) {
  if (array_a.size() != array_b.size()) {
    return false;
  }

  auto iter_a = array_a.begin();
  for (auto& item_b : array_b) {
    FXL_DCHECK(iter_a != array_a.end());
    if (*iter_a != item_b) {
      return false;
    }

    ++iter_a;
  }

  return true;
}

template <typename T>
bool operator!=(const fidl::Array<T>& array_a, const fidl::Array<T>& array_b) {
  return !(array_a == array_b);
}

bool operator==(const netstack::NetAddressPtr& addr_a,
                const netstack::NetAddressPtr& addr_b) {
  return (addr_a.get() == addr_b.get()) ||
         (addr_a && addr_a->family == addr_b->family &&
          addr_a->ipv4 == addr_b->ipv4 && addr_a->ipv6 == addr_b->ipv6);
}

bool operator==(const netstack::SocketAddressPtr& addr_a,
                const netstack::SocketAddressPtr& addr_b) {
  return (addr_a.get() == addr_b.get()) ||
         (addr_a && addr_a->port == addr_b->port &&
          addr_a->addr == addr_b->addr);
}

bool operator!=(const netstack::SocketAddressPtr& addr_a,
                const netstack::SocketAddressPtr& addr_b) {
  return !(addr_a == addr_b);
}
}  // namespace

ServiceSubscriber::ServiceSubscriber() {}

ServiceSubscriber::~ServiceSubscriber() {
  Reset();
}

void ServiceSubscriber::Init(MdnsServiceSubscriptionPtr subscription,
                             const UpdateCallback& callback) {
  subscription_ = std::move(subscription);
  callback_ = callback;
  HandleInstanceUpdates();
}

MdnsServiceSubscriptionPtr ServiceSubscriber::Reset() {
  callback_ = nullptr;
  instances_.reset();
  return std::move(subscription_);
}

void ServiceSubscriber::HandleInstanceUpdates(
    uint64_t version,
    fidl::Array<MdnsServiceInstancePtr> instances) {
  FXL_DCHECK(subscription_);

  if (instances) {
    if (callback_) {
      IssueCallbacks(instances);
    }

    instances_ = std::move(instances);
  }

  subscription_->GetInstances(
      version,
      [this](uint64_t version, fidl::Array<MdnsServiceInstancePtr> instances) {
        HandleInstanceUpdates(version, std::move(instances));
      });
}

void ServiceSubscriber::IssueCallbacks(
    const fidl::Array<MdnsServiceInstancePtr>& instances) {
  // For each instance in the update, see if it represents a new instance or
  // a change with respect to an old instance.
  for (auto& new_instance : instances) {
    bool found = false;

    // Search the old instances to see if there's a match.
    for (auto& old_instance : instances_) {
      if (new_instance->service_name == old_instance->service_name &&
          new_instance->instance_name == old_instance->instance_name) {
        // Found a match. If there's been a change, issue a callback to
        // indicate that.
        if (new_instance->v4_address != old_instance->v4_address ||
            new_instance->v6_address != old_instance->v6_address ||
            new_instance->text != old_instance->text) {
          callback_(old_instance.get(), new_instance.get());
        }

        found = true;
        break;
      }
    }

    if (!found) {
      // No match was found. Issue a callback indicating a new instance.
      callback_(nullptr, new_instance.get());
    }
  }

  // For each old instance, determine whether it has been removed.
  for (auto& old_instance : instances_) {
    bool found = false;

    // Search the new instances to see if there's a match.
    for (auto& new_instance : instances) {
      if (new_instance->service_name == old_instance->service_name &&
          new_instance->instance_name == old_instance->instance_name) {
        found = true;
        break;
      }
    }

    if (!found) {
      // No match was found. Issue a callback indicating a removed instance.
      callback_(old_instance.get(), nullptr);
    }
  }
}

}  // namespace mdns
