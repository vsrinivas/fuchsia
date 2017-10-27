// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_service_impl.h"

#include "garnet/bin/netconnector/mdns/mdns_fidl_util.h"
#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

MdnsServiceImpl::MdnsServiceImpl() {}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::AddBinding(fidl::InterfaceRequest<MdnsService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void MdnsServiceImpl::Start(const std::string& host_name,
                            const fxl::Closure& callback) {
  mdns_.Start(host_name, callback);
}

void MdnsServiceImpl::SubscribeToService(
    const std::string& service_name,
    const Mdns::ServiceInstanceCallback& callback) {
  auto iter = subscriptions_by_service_name_.find(service_name);

  if (iter == subscriptions_by_service_name_.end()) {
    auto pair = subscriptions_by_service_name_.emplace(
        service_name,
        std::make_unique<MdnsServiceSubscriptionImpl>(this, service_name));
    FXL_DCHECK(pair.second);
    iter = pair.first;
  }

  iter->second->SetCallback(callback);
}

void MdnsServiceImpl::PublishServiceInstance(
    const std::string& service_name,
    const std::string& instance_name,
    IpPort port,
    const std::vector<std::string>& text) {
  mdns_.PublishServiceInstance(service_name, instance_name, port, text,
                               [](MdnsResult result) {});
}

void MdnsServiceImpl::ResolveHostName(const fidl::String& host_name,
                                      uint32_t timeout_ms,
                                      const ResolveHostNameCallback& callback) {
  if (!MdnsNames::IsValidHostName(host_name)) {
    callback(nullptr, nullptr);
    return;
  }

  mdns_.ResolveHostName(
      host_name,
      fxl::TimePoint::Now() + fxl::TimeDelta::FromMilliseconds(timeout_ms),
      [this, callback](const std::string& host_name,
                       const IpAddress& v4_address,
                       const IpAddress& v6_address) {
        callback(MdnsFidlUtil::CreateSocketAddressIPv4(v4_address),
                 MdnsFidlUtil::CreateSocketAddressIPv6(v6_address));
      });
}

void MdnsServiceImpl::SubscribeToService(
    const fidl::String& service_name,
    fidl::InterfaceRequest<MdnsServiceSubscription> subscription_request) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    subscription_request = nullptr;
    return;
  }

  auto iter = subscriptions_by_service_name_.find(service_name);

  if (iter == subscriptions_by_service_name_.end()) {
    auto pair = subscriptions_by_service_name_.emplace(
        service_name,
        std::make_unique<MdnsServiceSubscriptionImpl>(this, service_name));
    FXL_DCHECK(pair.second);
    iter = pair.first;
  }

  iter->second->AddBinding(std::move(subscription_request));
}

void MdnsServiceImpl::PublishServiceInstance(
    const fidl::String& service_name,
    const fidl::String& instance_name,
    uint16_t port,
    fidl::Array<fidl::String> text,
    const PublishServiceInstanceCallback& callback) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    callback(MdnsResult::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    callback(MdnsResult::INVALID_INSTANCE_NAME);
    return;
  }

  mdns_.PublishServiceInstance(service_name, instance_name,
                               IpPort::From_uint16_t(port),
                               text.To<std::vector<std::string>>(), callback);
}

void MdnsServiceImpl::UnpublishServiceInstance(
    const fidl::String& service_name,
    const fidl::String& instance_name) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  mdns_.UnpublishServiceInstance(service_name, instance_name);
}

void MdnsServiceImpl::AddResponder(
    const fidl::String& service_name,
    const fidl::String& instance_name,
    fidl::InterfaceHandle<MdnsResponder> responder_handle) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    auto responder = MdnsResponderPtr::Create(std::move(responder_handle));
    responder->UpdateStatus(MdnsResult::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    auto responder = MdnsResponderPtr::Create(std::move(responder_handle));
    responder->UpdateStatus(MdnsResult::INVALID_INSTANCE_NAME);
    return;
  }

  mdns_.AddResponder(service_name, instance_name, std::move(responder_handle));
}

void MdnsServiceImpl::SetSubtypes(const fidl::String& service_name,
                                  const fidl::String& instance_name,
                                  fidl::Array<fidl::String> subtypes) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  mdns_.SetSubtypes(service_name, instance_name,
                    subtypes.To<std::vector<std::string>>());
}

void MdnsServiceImpl::ReannounceInstance(const fidl::String& service_name,
                                         const fidl::String& instance_name) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  mdns_.ReannounceInstance(service_name, instance_name);
}

void MdnsServiceImpl::SetVerbose(bool value) {
  mdns_.SetVerbose(value);
}

MdnsServiceImpl::MdnsServiceSubscriptionImpl::MdnsServiceSubscriptionImpl(
    MdnsServiceImpl* owner,
    const std::string& service_name)
    : owner_(owner) {
  bindings_.set_on_empty_set_handler([this, service_name]() {
    if (!callback_) {
      FXL_DCHECK(agent_);
      FXL_DCHECK(owner_);
      agent_->Quit();
      owner_->subscriptions_by_service_name_.erase(service_name);
    }
  });

  instances_publisher_.SetCallbackRunner(
      [this](const GetInstancesCallback& callback, uint64_t version) {
        fidl::Array<MdnsServiceInstancePtr> instances =
            fidl::Array<MdnsServiceInstancePtr>::New(0);

        for (auto& pair : instances_by_name_) {
          instances.push_back(pair.second.Clone());
        }

        callback(version, std::move(instances));
      });

  agent_ = owner->mdns_.SubscribeToService(
      service_name,
      [this](const std::string& service, const std::string& instance,
             const SocketAddress& v4_address, const SocketAddress& v6_address,
             const std::vector<std::string>& text) {
        if (callback_) {
          callback_(service, instance, v4_address, v6_address, text);
        }

        bool changed = false;

        if (v4_address.is_valid() || v6_address.is_valid()) {
          auto iter = instances_by_name_.find(instance);
          if (iter == instances_by_name_.end()) {
            instances_by_name_.emplace(
                instance, MdnsFidlUtil::CreateServiceInstance(
                              service, instance, v4_address, v6_address, text));
            changed = true;
          } else {
            changed = MdnsFidlUtil::UpdateServiceInstance(
                iter->second, v4_address, v6_address, text);
          }
        } else {
          changed = instances_by_name_.erase(instance) != 0;
        }

        if (changed) {
          instances_publisher_.SendUpdates();
        }
      });
}

MdnsServiceImpl::MdnsServiceSubscriptionImpl::~MdnsServiceSubscriptionImpl() {}

void MdnsServiceImpl::MdnsServiceSubscriptionImpl::AddBinding(
    fidl::InterfaceRequest<MdnsServiceSubscription> subscription_request) {
  bindings_.AddBinding(this, std::move(subscription_request));
}

void MdnsServiceImpl::MdnsServiceSubscriptionImpl::GetInstances(
    uint64_t version_last_seen,
    const GetInstancesCallback& callback) {
  instances_publisher_.Get(version_last_seen, callback);
}

}  // namespace mdns
}  // namespace netconnector
