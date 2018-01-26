// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_service_impl.h"

#include "garnet/bin/mdns/service/fidl_interface_monitor.h"
#include "garnet/bin/mdns/service/host_name.h"
#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "garnet/bin/mdns/service/mdns_names.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace mdns {

MdnsServiceImpl::MdnsServiceImpl(app::ApplicationContext* application_context)
    : application_context_(application_context) {
  application_context_->outgoing_services()->AddService<MdnsService>(
      [this](fidl::InterfaceRequest<MdnsService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  Start();
}

MdnsServiceImpl::~MdnsServiceImpl() {}

void MdnsServiceImpl::Start() {
  // TODO(NET-79): Remove this check when NET-79 is fixed.
  if (!NetworkIsReady()) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() { Start(); }, fxl::TimeDelta::FromSeconds(5));
    return;
  }

  mdns_.Start(FidlInterfaceMonitor::Create(application_context_),
              GetHostName());
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

  auto publisher = std::unique_ptr<SimplePublisher>(new SimplePublisher(
      IpPort::From_uint16_t(port), std::move(text), callback));

  if (!mdns_.PublishServiceInstance(service_name, instance_name,
                                    publisher.get())) {
    callback(MdnsResult::ALREADY_PUBLISHED_LOCALLY);
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

void MdnsServiceImpl::UnpublishServiceInstance(
    const fidl::String& service_name,
    const fidl::String& instance_name) {
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
    const fidl::String& service_name,
    const fidl::String& instance_name,
    fidl::InterfaceHandle<MdnsResponder> responder_handle) {
  FXL_DCHECK(responder_handle);

  auto responder_ptr = MdnsResponderPtr::Create(std::move(responder_handle));
  FXL_DCHECK(responder_ptr);

  if (!MdnsNames::IsValidServiceName(service_name)) {
    responder_ptr->UpdateStatus(MdnsResult::INVALID_SERVICE_NAME);
    return;
  }

  if (!MdnsNames::IsValidInstanceName(instance_name)) {
    responder_ptr->UpdateStatus(MdnsResult::INVALID_INSTANCE_NAME);
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
    publisher->responder_->UpdateStatus(MdnsResult::ALREADY_PUBLISHED_LOCALLY);
    return;
  }

  // |Mdns| told us our instance is unique locally, so the full name should
  // not appear in our collection.
  FXL_DCHECK(publishers_by_instance_full_name_.find(instance_full_name) ==
             publishers_by_instance_full_name_.end());

  publishers_by_instance_full_name_.emplace(instance_full_name,
                                            std::move(publisher));
}

void MdnsServiceImpl::SetSubtypes(const fidl::String& service_name,
                                  const fidl::String& instance_name,
                                  fidl::Array<fidl::String> subtypes) {
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

  iter->second->SetSubtypes(subtypes.To<std::vector<std::string>>());
}

void MdnsServiceImpl::ReannounceInstance(const fidl::String& service_name,
                                         const fidl::String& instance_name) {
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

void MdnsServiceImpl::SetVerbose(bool value) {
  mdns_.SetVerbose(value);
}

MdnsServiceImpl::Subscriber::Subscriber(
    fidl::InterfaceRequest<MdnsServiceSubscription> request,
    const fxl::Closure& deleter)
    : binding_(this, std::move(request)) {
  binding_.set_connection_error_handler([this, deleter]() {
    binding_.set_connection_error_handler(nullptr);
    deleter();
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
}

MdnsServiceImpl::Subscriber::~Subscriber() {}

void MdnsServiceImpl::Subscriber::InstanceDiscovered(
    const std::string& service,
    const std::string& instance,
    const SocketAddress& v4_address,
    const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  instances_by_name_.emplace(
      instance, MdnsFidlUtil::CreateServiceInstance(
                    service, instance, v4_address, v6_address, text));
}

void MdnsServiceImpl::Subscriber::InstanceChanged(
    const std::string& service,
    const std::string& instance,
    const SocketAddress& v4_address,
    const SocketAddress& v6_address,
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

void MdnsServiceImpl::Subscriber::GetInstances(
    uint64_t version_last_seen,
    const GetInstancesCallback& callback) {
  instances_publisher_.Get(version_last_seen, callback);
}

MdnsServiceImpl::SimplePublisher::SimplePublisher(
    IpPort port,
    fidl::Array<fidl::String> text,
    const PublishServiceInstanceCallback& callback)
    : port_(port),
      text_(text.To<std::vector<std::string>>()),
      callback_(callback) {}

void MdnsServiceImpl::SimplePublisher::ReportSuccess(bool success) {
  callback_(success ? MdnsResult::OK : MdnsResult::ALREADY_PUBLISHED_ON_SUBNET);
}

void MdnsServiceImpl::SimplePublisher::GetPublication(
    bool query,
    const std::string& subtype,
    const std::function<void(std::unique_ptr<Mdns::Publication>)>& callback) {
  callback(Mdns::Publication::Create(port_, text_));
}

MdnsServiceImpl::ResponderPublisher::ResponderPublisher(
    MdnsResponderPtr responder,
    const fxl::Closure& deleter)
    : responder_(std::move(responder)) {
  FXL_DCHECK(responder_);

  responder_.set_connection_error_handler([this, deleter]() {
    responder_.set_connection_error_handler(nullptr);
    deleter();
  });
}

void MdnsServiceImpl::ResponderPublisher::ReportSuccess(bool success) {
  FXL_DCHECK(responder_);
  responder_->UpdateStatus(success ? MdnsResult::OK
                                   : MdnsResult::ALREADY_PUBLISHED_ON_SUBNET);
}

void MdnsServiceImpl::ResponderPublisher::GetPublication(
    bool query,
    const std::string& subtype,
    const std::function<void(std::unique_ptr<Mdns::Publication>)>& callback) {
  FXL_DCHECK(responder_);
  responder_->GetPublication(query, subtype,
                             [callback](MdnsPublicationPtr publication_ptr) {
                               callback(MdnsFidlUtil::Convert(publication_ptr));
                             });
}

}  // namespace mdns
