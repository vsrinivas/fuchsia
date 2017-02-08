// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "application/lib/app/application_context.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/netconnector/services/netconnector.fidl.h"
#include "apps/netconnector/services/netconnector_admin.fidl.h"
#include "apps/netconnector/src/device_service_provider.h"
#include "apps/netconnector/src/listener.h"
#include "apps/netconnector/src/netconnector_params.h"
#include "apps/netconnector/src/requestor_agent.h"
#include "apps/netconnector/src/responding_service_host.h"
#include "apps/netconnector/src/service_agent.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace netconnector {

class NetConnectorImpl : public NetConnector, public NetConnectorAdmin {
 public:
  NetConnectorImpl(NetConnectorParams* params);

  ~NetConnectorImpl();

  modular::ServiceProvider* responding_services() {
    return responding_service_host_.services();
  }

  void ReleaseDeviceServiceProvider(
      DeviceServiceProvider* device_service_provider);

  void AddRequestorAgent(std::unique_ptr<RequestorAgent> requestor_agent);

  void ReleaseRequestorAgent(RequestorAgent* requestor_agent);

  void ReleaseServiceAgent(ServiceAgent* service_agent);

  // NetConnector implementation.
  void GetDeviceServiceProvider(const fidl::String& device_name,
                                fidl::InterfaceRequest<modular::ServiceProvider>
                                    service_provider) override;

  // NetConnectorAdmin implementation.
  void SetHostName(const fidl::String& host_name) override;

  void RegisterService(const fidl::String& name,
                       modular::ApplicationLaunchInfoPtr launch_info) override;

  void RegisterDevice(const fidl::String& name,
                      const fidl::String& address) override;

  void RegisterServiceProvider(const fidl::String& name,
                               fidl::InterfaceHandle<modular::ServiceProvider>
                                   service_provider) override;

 private:
  static constexpr uint16_t kPort = 7777;

  void AddDeviceServiceProvider(
      std::unique_ptr<DeviceServiceProvider> device_service_provider);

  void AddServiceAgent(std::unique_ptr<ServiceAgent> service_agent);

  NetConnectorParams* params_;
  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<NetConnector> bindings_;
  fidl::BindingSet<NetConnectorAdmin> admin_bindings_;
  Listener listener_;
  RespondingServiceHost responding_service_host_;
  std::unordered_map<DeviceServiceProvider*,
                     std::unique_ptr<DeviceServiceProvider>>
      device_service_providers_;
  std::unordered_map<RequestorAgent*, std::unique_ptr<RequestorAgent>>
      requestor_agents_;
  std::unordered_map<ServiceAgent*, std::unique_ptr<ServiceAgent>>
      service_agents_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorImpl);
};

}  // namespace netconnector
