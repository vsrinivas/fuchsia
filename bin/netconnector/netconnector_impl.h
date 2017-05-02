// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "application/lib/app/application_context.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "apps/netconnector/services/netconnector.fidl.h"
#include "apps/netconnector/src/device_service_provider.h"
#include "apps/netconnector/src/ip_port.h"
#include "apps/netconnector/src/listener.h"
#include "apps/netconnector/src/mdns/mdns_service_impl.h"
#include "apps/netconnector/src/netconnector_params.h"
#include "apps/netconnector/src/requestor_agent.h"
#include "apps/netconnector/src/responding_service_host.h"
#include "apps/netconnector/src/service_agent.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace netconnector {

class NetConnectorImpl : public NetConnector {
 public:
  NetConnectorImpl(NetConnectorParams* params);

  ~NetConnectorImpl() override;

  // Returns the service provider exposed to remote requestors.
  app::ServiceProvider* responding_services() {
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
  void RegisterServiceProvider(
      const fidl::String& name,
      fidl::InterfaceHandle<app::ServiceProvider> service_provider) override;

  void GetDeviceServiceProvider(
      const fidl::String& device_name,
      fidl::InterfaceRequest<app::ServiceProvider> service_provider) override;

  void GetKnownDeviceNames(
      uint64_t version_last_seen,
      const GetKnownDeviceNamesCallback& callback) override;

 private:
  static const IpPort kPort;
  static const std::string kFuchsiaServiceName;

  void AddDeviceServiceProvider(
      std::unique_ptr<DeviceServiceProvider> device_service_provider);

  void AddServiceAgent(std::unique_ptr<ServiceAgent> service_agent);

  void StartMdns();

  NetConnectorParams* params_;
  std::unique_ptr<app::ApplicationContext> application_context_;
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

  mdns::MdnsServiceImpl mdns_service_impl_;

  media::FidlPublisher<GetKnownDeviceNamesCallback> device_names_publisher_;

  // TODO(dalesat): Temporary hack until we have a real gethostname. Remove.
  uint32_t mdns_start_attempts_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorImpl);
};

}  // namespace netconnector
