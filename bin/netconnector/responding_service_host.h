// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_namespace.h"

namespace netconnector {

// Provides services based on service registrations.
class RespondingServiceHost {
 public:
  RespondingServiceHost(const app::ApplicationEnvironmentPtr& environment);

  ~RespondingServiceHost();

  // Registers a singleton service.
  void RegisterSingleton(const std::string& service_name,
                         app::ApplicationLaunchInfoPtr launch_info);

  // Registers a provider for a singleton service.
  void RegisterProvider(const std::string& service_name,
                        fidl::InterfaceHandle<app::ServiceProvider> handle);

  app::ServiceProvider* services() {
    return static_cast<app::ServiceProvider*>(&service_namespace_);
  }

  // Adds a binding to the service provider.
  void AddBinding(fidl::InterfaceRequest<app::ServiceProvider> request) {
    service_namespace_.AddBinding(std::move(request));
  }

 private:
  std::unordered_map<std::string, app::ServiceProviderPtr>
      service_providers_by_name_;
  app::ServiceNamespace service_namespace_;
  app::ApplicationLauncherPtr launcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RespondingServiceHost);
};

}  // namespace netconnector
