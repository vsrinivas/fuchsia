// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_service_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/mdns/service/fidl_interface_monitor.h"
#include "garnet/bin/mdns/service/host_name.h"
#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "garnet/bin/mdns/service/mdns_names.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/type_converter.h"

namespace mdns {

MdnsServiceImpl::MdnsServiceImpl(component::StartupContext* startup_context)
    : startup_context_(startup_context) {
  startup_context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  Start();
}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::Start() {
  // TODO(NET-79): Remove this check when NET-79 is fixed.
  if (!NetworkIsReady()) {
    async::PostDelayedTask(async_get_default_dispatcher(),
                           [this]() { Start(); }, zx::sec(5));
    return;
  }

  mdns_.Start(FidlInterfaceMonitor::Create(startup_context_), GetHostName());
}

void MdnsServiceImpl::ResolveHostName(fidl::StringPtr host_name,
                                      uint32_t timeout_ms,
                                      ResolveHostNameCallback callback) {
  if (!MdnsNames::IsValidHostName(host_name)) {
    callback(nullptr, nullptr);
    return;
  }

  mdns_.ResolveHostName(
      host_name,
      fxl::TimePoint::Now() + fxl::TimeDelta::FromMilliseconds(timeout_ms),
      [this, callback = std::move(callback)](const std::string& host_name,
                                             const IpAddress& v4_address,
                                             const IpAddress& v6_address) {
        callback(MdnsFidlUtil::CreateSocketAddressIPv4(v4_address),
                 MdnsFidlUtil::CreateSocketAddressIPv6(v6_address));
      });
}

void MdnsServiceImpl::SubscribeToService(
    fidl::StringPtr service_name,
    fidl::InterfaceRequest<fuchsia::mdns::MdnsServiceSubscription>
        subscription_request) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    return;
  }

  size_t id = next_subscriber_id_++;
  auto subscriber = std::make_unique<Subscriber>(
      std::move(subscription_request),
      [this, id]() { subscribers_by_id_.erase(id); });

  mdns_.SubscribeToService(service_name, subscriber.get());

  subscribers_by_id_.emplace(id, std::move(subscriber));
}

void MdnsServiceImpl::PublishServiceInstance(
    fidl::StringPtr service_name, fidl::StringPtr instance_name, uint16_t port,
    fidl::VectorPtr<fidl::StringPtr> text,
    PublishServiceInstanceCallback callback) {
  if (!MdnsNames::IsValidServiceName(service_name)) {
    callback(fuchsia::mdns::MdnsResult::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    callback(fuchsia::mdns::MdnsResult::INVALID_INSTANCE_NAME);
    return;
  }

  auto publisher = std::unique_ptr<SimplePublisher>(new SimplePublisher(
      IpPort::From_uint16_t(port), std::move(text), callback.share()));

  if (!mdns_.PublishServiceInstance(service_name, instance_name,
                                    publisher.get())) {
    callback(fuchsia::mdns::MdnsResult::ALREADY_PUBLISHED_LOCALLY);
    return;
  }

  MdnsNames::LocalInstanceFullName(instance_name, service_name);

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name,
                                            std::move(publisher));
}

void MdnsServiceImpl::UnpublishServiceInstance(fidl::StringPtr service_name,
                                               fidl::StringPtr instance_name) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  // This will delete the publisher, unpublishing the service instance.
  publishers_by_instance_full_name_.erase(instance_full_name);
}

void MdnsServiceImpl::AddResponder(
    fidl::StringPtr service_name, fidl::StringPtr instance_name,
    fidl::InterfaceHandle<fuchsia::mdns::MdnsResponder> responder_handle) {
  FXL_DCHECK(responder_handle);

  auto responder_ptr = responder_handle.Bind();
  FXL_DCHECK(responder_ptr);

  if (!MdnsNames::IsValidServiceName(service_name)) {
    responder_ptr->UpdateStatus(
        fuchsia::mdns::MdnsResult::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    responder_ptr->UpdateStatus(
        fuchsia::mdns::MdnsResult::INVALID_INSTANCE_NAME);
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto publisher = std::unique_ptr<ResponderPublisher>(new ResponderPublisher(
      std::move(responder_ptr), [this, instance_full_name]() {
        publishers_by_instance_full_name_.erase(instance_full_name);
      }));

  if (!mdns_.PublishServiceInstance(service_name, instance_name,
                                    publisher.get())) {
    publisher->responder_->UpdateStatus(
        fuchsia::mdns::MdnsResult::ALREADY_PUBLISHED_LOCALLY);
    return;
  }

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name,
                                            std::move(publisher));
}

void MdnsServiceImpl::SetSubtypes(fidl::StringPtr service_name,
                                  fidl::StringPtr instance_name,
                                  fidl::VectorPtr<fidl::StringPtr> subtypes) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto iter = publishers_by_instance_full_name_.find(instance_full_name);
  if (iter == publishers_by_instance_full_name_.end()) {
    return;
  }

  iter->second->SetSubtypes(fxl::To<std::vector<std::string>>(subtypes));
}

void MdnsServiceImpl::ReannounceInstance(fidl::StringPtr service_name,
                                         fidl::StringPtr instance_name) {
  if (!MdnsNames::IsValidServiceName(service_name) ||
      !MdnsNames::IsValidInstanceName(instance_name)) {
    return;
  }

  std::string instance_full_name =
      MdnsNames::LocalInstanceFullName(instance_name, service_name);

  auto iter = publishers_by_instance_full_name_.find(instance_full_name);
  if (iter == publishers_by_instance_full_name_.end()) {
    return;
  }

  iter->second->Reannounce();
}

void MdnsServiceImpl::SetVerbose(bool value) { mdns_.SetVerbose(value); }

MdnsServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceRequest<fuchsia::mdns::MdnsServiceSubscription> request,
    fit::closure deleter)
    : binding_(this, std::move(request)) {
  binding_.set_error_handler([this, deleter = std::move(deleter)]() {
    binding_.set_error_handler(nullptr);
    deleter();
  });

  instances_publisher_.SetCallbackRunner(
      [this](GetInstancesCallback callback, uint64_t version) {
        fidl::VectorPtr<fuchsia::mdns::MdnsServiceInstance> instances(
            instances_by_name_.size());

        size_t i = 0;
        for (auto& pair : instances_by_name_) {
          pair.second->Clone(&instances->at(i++));
        }

        callback(version, std::move(instances));
      });
}

MdnsServiceImpl::Subscriber::~Subscriber() {}

void MdnsServiceImpl::Subscriber::InstanceDiscovered(
    const std::string& service, const std::string& instance,
    const SocketAddress& v4_address, const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  instances_by_name_.emplace(
      instance, MdnsFidlUtil::CreateServiceInstance(
                    service, instance, v4_address, v6_address, text));
}

void MdnsServiceImpl::Subscriber::InstanceChanged(
    const std::string& service, const std::string& instance,
    const SocketAddress& v4_address, const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  auto iter = instances_by_name_.find(instance);
  if (iter != instances_by_name_.end()) {
    MdnsFidlUtil::UpdateServiceInstance(iter->second, v4_address, v6_address,
                                        text);
  }
}

void MdnsServiceImpl::Subscriber::InstanceLost(const std::string& service,
                                               const std::string& instance) {
  instances_by_name_.erase(instance);
}

void MdnsServiceImpl::Subscriber::UpdatesComplete() {
  instances_publisher_.SendUpdates();
}

void MdnsServiceImpl::Subscriber::GetInstances(uint64_t version_last_seen,
                                               GetInstancesCallback callback) {
  instances_publisher_.Get(version_last_seen, std::move(callback));
}

MdnsServiceImpl::SimplePublisher::SimplePublisher(
    IpPort port, fidl::VectorPtr<fidl::StringPtr> text,
    PublishServiceInstanceCallback callback)
    : port_(port),
      text_(fxl::To<std::vector<std::string>>(text)),
      callback_(std::move(callback)) {}

void MdnsServiceImpl::SimplePublisher::ReportSuccess(bool success) {
  callback_(success ? fuchsia::mdns::MdnsResult::OK
                    : fuchsia::mdns::MdnsResult::ALREADY_PUBLISHED_ON_SUBNET);
}

void MdnsServiceImpl::SimplePublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  callback(Mdns::Publication::Create(port_, text_));
}

MdnsServiceImpl::ResponderPublisher::ResponderPublisher(
    fuchsia::mdns::MdnsResponderPtr responder, fit::closure deleter)
    : responder_(std::move(responder)) {
  FXL_DCHECK(responder_);

  responder_.set_error_handler([this, deleter = std::move(deleter)]() {
    responder_.set_error_handler(nullptr);
    deleter();
  });
}

void MdnsServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  FXL_DCHECK(responder_);
  responder_->UpdateStatus(
      success ? fuchsia::mdns::MdnsResult::OK
              : fuchsia::mdns::MdnsResult::ALREADY_PUBLISHED_ON_SUBNET);
}

void MdnsServiceImpl::ResponderPublisher::GetPublication(
    bool query, const std::string& subtype,
    fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) {
  FXL_DCHECK(responder_);
  responder_->GetPublication(
      query, subtype,
      fxl::MakeCopyable([callback = std::move(callback)](
                            fuchsia::mdns::MdnsPublicationPtr publication_ptr) {
        callback(MdnsFidlUtil::Convert(publication_ptr));
      }));
}

}  // namespace mdns
