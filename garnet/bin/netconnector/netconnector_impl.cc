// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/netconnector_impl.h"

#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "garnet/bin/netconnector/device_service_provider.h"
#include "garnet/bin/netconnector/host_name.h"
#include "garnet/bin/netconnector/netconnector_params.h"
#include "src/lib/fxl/logging.h"

namespace netconnector {

// static
const inet::IpPort NetConnectorImpl::kPort = inet::IpPort::From_uint16_t(7777);
// static
const std::string NetConnectorImpl::kFuchsiaServiceName = "_fuchsia._tcp.";
// static
const std::string NetConnectorImpl::kLocalDeviceName = "local";

NetConnectorImpl::NetConnectorImpl(NetConnectorParams* params,
                                   fit::closure quit_callback)
    : params_(params),
      quit_callback_(std::move(quit_callback)),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      // TODO(dalesat): Create a new RespondingServiceHost per user.
      // Requestors should provide user credentials allowing a ServiceAgent
      // to obtain a user environment. A RespondingServiceHost should be
      // created with that environment so that responding services are
      // launched in the correct environment.
      responding_service_host_(startup_context_->environment()),
      mdns_subscriber_binding_(this) {
  FXL_DCHECK(quit_callback_);

  if (!params->listen()) {
    // Start the listener.
    fuchsia::netconnector::NetConnectorSyncPtr net_connector;
    startup_context_->ConnectToEnvironmentService(net_connector.NewRequest());
    fuchsia::mdns::ControllerPtr mdns_service =
        startup_context_
            ->ConnectToEnvironmentService<fuchsia::mdns::Controller>();

    if (params_->mdns_verbose()) {
      mdns_service->DEPRECATEDSetVerbose(true);
    }

    if (params_->show_devices()) {
      uint64_t version;
      std::vector<std::string> device_names;
      net_connector->GetKnownDeviceNames(
          fuchsia::netconnector::kInitialKnownDeviceNames, &version,
          &device_names);

      if (device_names.size() == 0) {
        std::cout << "No remote devices found\n";
      } else {
        for (auto& device_name : device_names) {
          std::cout << device_name << "\n";
        }
      }
    }

    quit_callback_();
    return;
  }

  // Running as listener.
  startup_context_->outgoing().AddPublicService(bindings_.GetHandler(this));

  device_names_publisher_.SetCallbackRunner(
      [this](const GetKnownDeviceNamesCallback& callback, uint64_t version) {
        fidl::VectorPtr<std::string> device_names =
            fidl::VectorPtr<std::string>::New(0);

        for (auto& pair : params_->devices()) {
          device_names.push_back(pair.first);
        }

        callback(version, std::move(device_names));
      });

  // Register services.
  for (auto& pair : params->MoveServices()) {
    responding_service_host_.RegisterSingleton(pair.first,
                                               std::move(pair.second));
  }

  StartListener();
}

NetConnectorImpl::~NetConnectorImpl() {}

void NetConnectorImpl::StartListener() {
  if (!NetworkIsReady()) {
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { StartListener(); },
        zx::sec(5));
    return;
  }

  listener_.Start(kPort, [this](fxl::UniqueFD fd) {
    AddServiceAgent(ServiceAgent::Create(std::move(fd), this));
  });

  mdns_controller_ =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::mdns::Controller>();

  host_name_ = GetHostName();

  mdns_controller_->DEPRECATEDPublishServiceInstance(
      kFuchsiaServiceName, host_name_, kPort.as_uint16_t(),
      fidl::VectorPtr<std::string>(), true,
      [this](fuchsia::mdns::Result result) {
        switch (result) {
          case fuchsia::mdns::Result::OK:
            break;
          case fuchsia::mdns::Result::INVALID_SERVICE_NAME:
            FXL_LOG(ERROR) << "mDNS service rejected service name "
                           << kFuchsiaServiceName << ".";
            break;
          case fuchsia::mdns::Result::INVALID_INSTANCE_NAME:
            FXL_LOG(ERROR) << "mDNS service rejected instance name "
                           << host_name_ << ".";
            break;
          case fuchsia::mdns::Result::ALREADY_PUBLISHED_LOCALLY:
            FXL_LOG(ERROR) << "mDNS service is already publishing a "
                           << kFuchsiaServiceName << " service instance.";
            break;
          case fuchsia::mdns::Result::ALREADY_PUBLISHED_ON_SUBNET:
            FXL_LOG(ERROR) << "Another device is already publishing a "
                           << kFuchsiaServiceName
                           << " service instance for this host's name ("
                           << host_name_ << ").";
            break;
        }
      });

  fidl::InterfaceHandle<fuchsia::mdns::ServiceSubscriber> subscriber_handle;

  mdns_subscriber_binding_.Bind(subscriber_handle.NewRequest());
  mdns_subscriber_binding_.set_error_handler([this](zx_status_t status) {
    mdns_subscriber_binding_.set_error_handler(nullptr);
    mdns_subscriber_binding_.Unbind();
  });

  mdns_controller_->SubscribeToService(kFuchsiaServiceName,
                                       std::move(subscriber_handle));
}

void NetConnectorImpl::ReleaseDeviceServiceProvider(
    DeviceServiceProvider* device_service_provider) {
  size_t removed = device_service_providers_.erase(device_service_provider);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseRequestorAgent(RequestorAgent* requestor_agent) {
  size_t removed = requestor_agents_.erase(requestor_agent);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseServiceAgent(ServiceAgent* service_agent) {
  size_t removed = service_agents_.erase(service_agent);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::GetDeviceServiceProvider(
    std::string device_name,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  if (device_name == host_name_ || device_name == kLocalDeviceName) {
    responding_service_host_.AddBinding(std::move(request));
    return;
  }

  auto iter = params_->devices().find(device_name);
  if (iter == params_->devices().end()) {
    FXL_LOG(ERROR) << "Unrecognized device name " << device_name;
    return;
  }

  AddDeviceServiceProvider(DeviceServiceProvider::Create(
      device_name, inet::SocketAddress(iter->second, kPort), std::move(request),
      this));
}

void NetConnectorImpl::GetKnownDeviceNames(
    uint64_t version_last_seen, GetKnownDeviceNamesCallback callback) {
  device_names_publisher_.Get(version_last_seen, std::move(callback));
}

void NetConnectorImpl::RegisterServiceProvider(
    std::string name,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle) {
  FXL_LOG(INFO) << "Service '" << name << "' provider registered.";
  responding_service_host_.RegisterProvider(name, std::move(handle));
}

void NetConnectorImpl::AddDeviceServiceProvider(
    std::unique_ptr<DeviceServiceProvider> device_service_provider) {
  DeviceServiceProvider* raw_ptr = device_service_provider.get();
  device_service_providers_.emplace(raw_ptr,
                                    std::move(device_service_provider));
}

void NetConnectorImpl::AddRequestorAgent(
    std::unique_ptr<RequestorAgent> requestor_agent) {
  RequestorAgent* raw_ptr = requestor_agent.get();
  requestor_agents_.emplace(raw_ptr, std::move(requestor_agent));
}

void NetConnectorImpl::AddServiceAgent(
    std::unique_ptr<ServiceAgent> service_agent) {
  ServiceAgent* raw_ptr = service_agent.get();
  service_agents_.emplace(raw_ptr, std::move(service_agent));
}

void NetConnectorImpl::InstanceDiscovered(
    fuchsia::mdns::ServiceInstance instance,
    InstanceDiscoveredCallback callback) {
  if (instance.v4_address) {
    std::cerr << "netconnector: Device '" << instance.instance_name
              << "' discovered at address "
              << inet::SocketAddress(instance.v4_address.get()) << "\n";
    params_->RegisterDevice(instance.instance_name,
                            inet::IpAddress(&instance.v4_address->addr));
  } else {
    FXL_DCHECK(instance.v6_address);
    std::cerr << "netconnector: Device '" << instance.instance_name
              << "' discovered at address "
              << inet::SocketAddress(instance.v6_address.get()) << "\n";
    params_->RegisterDevice(instance.instance_name,
                            inet::IpAddress(&instance.v6_address->addr));
  }

  device_names_publisher_.SendUpdates();

  callback();
}

void NetConnectorImpl::InstanceChanged(fuchsia::mdns::ServiceInstance instance,
                                       InstanceChangedCallback callback) {
  callback();
}

void NetConnectorImpl::InstanceLost(std::string service_name,
                                    std::string instance_name,
                                    InstanceLostCallback callback) {
  std::cerr << "netconnector: Device '" << instance_name << "' lost\n";
  params_->UnregisterDevice(instance_name);
  device_names_publisher_.SendUpdates();
  callback();
}

}  // namespace netconnector
