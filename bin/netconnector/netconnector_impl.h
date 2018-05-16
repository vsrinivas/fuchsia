// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <component/cpp/fidl.h>
#include <mdns/cpp/fidl.h>
#include <netconnector/cpp/fidl.h>

#include "garnet/bin/media/util/fidl_publisher.h"
#include "garnet/bin/netconnector/device_service_provider.h"
#include "garnet/bin/netconnector/ip_port.h"
#include "garnet/bin/netconnector/listener.h"
#include "garnet/bin/netconnector/netconnector_params.h"
#include "garnet/bin/netconnector/requestor_agent.h"
#include "garnet/bin/netconnector/responding_service_host.h"
#include "garnet/bin/netconnector/service_agent.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/mdns/cpp/service_subscriber.h"

namespace netconnector {

class NetConnectorImpl : public NetConnector {
 public:
  NetConnectorImpl(NetConnectorParams* params, fxl::Closure quit_callback);

  ~NetConnectorImpl() override;

  // Returns the service provider exposed to remote requestors.
  component::ServiceProvider* responding_services() {
    return responding_service_host_.services();
  }

  // Releases a service provider for a remote device.
  void ReleaseDeviceServiceProvider(
      DeviceServiceProvider* device_service_provider);

  // Adds an agent that represents a local requestor.
  void AddRequestorAgent(std::unique_ptr<RequestorAgent> requestor_agent);

  // Releases an agent that manages a connection on behalf of a local requestor.
  void ReleaseRequestorAgent(RequestorAgent* requestor_agent);

  // Releases an agent that manages a connection on behalf of a remote
  // requestor.
  void ReleaseServiceAgent(ServiceAgent* service_agent);

  // NetConnector implementation.
  void RegisterServiceProvider(fidl::StringPtr name,
                               fidl::InterfaceHandle<component::ServiceProvider>
                                   service_provider) override;

  void GetDeviceServiceProvider(
      fidl::StringPtr device_name,
      fidl::InterfaceRequest<component::ServiceProvider> service_provider)
      override;

  void GetKnownDeviceNames(uint64_t version_last_seen,
                           GetKnownDeviceNamesCallback callback) override;

 private:
  static const IpPort kPort;
  static const std::string kFuchsiaServiceName;
  static const std::string kLocalDeviceName;

  void StartListener();

  void AddDeviceServiceProvider(
      std::unique_ptr<DeviceServiceProvider> device_service_provider);

  void AddServiceAgent(std::unique_ptr<ServiceAgent> service_agent);

  NetConnectorParams* params_;
  fxl::Closure quit_callback_;
  std::unique_ptr<component::ApplicationContext> application_context_;
  std::string host_name_;
  fidl::BindingSet<NetConnector> bindings_;
  Listener listener_;
  RespondingServiceHost responding_service_host_;
  std::unordered_map<DeviceServiceProvider*,
                     std::unique_ptr<DeviceServiceProvider>>
      device_service_providers_;
  std::unordered_map<RequestorAgent*, std::unique_ptr<RequestorAgent>>
      requestor_agents_;
  std::unordered_map<ServiceAgent*, std::unique_ptr<ServiceAgent>>
      service_agents_;

  mdns::MdnsServicePtr mdns_service_;
  mdns::ServiceSubscriber mdns_subscriber_;

  media::FidlPublisher<GetKnownDeviceNamesCallback> device_names_publisher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorImpl);
};

}  // namespace netconnector
