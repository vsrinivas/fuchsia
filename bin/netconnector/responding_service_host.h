// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETCONNECTOR_RESPONDING_SERVICE_HOST_H_
#define GARNET_BIN_NETCONNECTOR_RESPONDING_SERVICE_HOST_H_

#include <unordered_map>

#include <component/cpp/fidl.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_namespace.h"
#include "lib/svc/cpp/services.h"

namespace netconnector {

// Provides services based on service registrations.
class RespondingServiceHost {
 public:
  RespondingServiceHost(const component::EnvironmentPtr& environment);

  ~RespondingServiceHost();

  // Registers a singleton service.
  void RegisterSingleton(const std::string& service_name,
                         component::LaunchInfoPtr launch_info);

  // Registers a provider for a singleton service.
  void RegisterProvider(
      const std::string& service_name,
      fidl::InterfaceHandle<component::ServiceProvider> handle);

  component::ServiceProvider* services() {
    return static_cast<component::ServiceProvider*>(&service_namespace_);
  }

  // Adds a binding to the service provider.
  void AddBinding(fidl::InterfaceRequest<component::ServiceProvider> request) {
    service_namespace_.AddBinding(std::move(request));
  }

 private:
  class ServicesHolder {
   public:
    ServicesHolder(component::Services services,
                   component::ComponentControllerPtr controller)
        : services_(std::move(services)) {}
    ServicesHolder(component::ServiceProviderPtr service_provider)
        : service_provider_(std::move(service_provider)),
          is_service_provider_(true) {}
    void ConnectToService(const std::string& service_name, zx::channel c);

   private:
    component::Services services_;
    component::ServiceProviderPtr service_provider_;
    const bool is_service_provider_{};
  };
  std::unordered_map<std::string, ServicesHolder> service_providers_by_name_;

  component::ServiceNamespace service_namespace_;
  component::ApplicationLauncherPtr launcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RespondingServiceHost);
};

}  // namespace netconnector

#endif  // GARNET_BIN_NETCONNECTOR_RESPONDING_SERVICE_HOST_H_
