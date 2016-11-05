// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_APP_APPLICATION_CONTEXT_H_
#define APPS_MODULAR_LIB_APP_APPLICATION_CONTEXT_H_

#include <memory>

#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

class ApplicationContext {
 public:
  ApplicationContext(
      fidl::InterfaceHandle<ServiceProvider> environment_services,
      fidl::InterfaceRequest<ServiceProvider> outgoing_services);

  ~ApplicationContext();

  static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

  ServiceProvider* environment_services() const {
    return environment_services_.get();
  }

  ServiceProviderImpl* outgoing_services() { return &outgoing_services_; }

  const ApplicationEnvironmentPtr& environment() const { return environment_; }

  const ApplicationLauncherPtr& launcher() const { return launcher_; }

  // Helper for connecting to a service provided by the environment.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> interface_ptr;
    environment_services_->ConnectToService(
        interface_name, GetProxy(&interface_ptr).PassMessagePipe());
    return interface_ptr;
  }

 private:
  ServiceProviderPtr environment_services_;
  ServiceProviderImpl outgoing_services_;
  ApplicationEnvironmentPtr environment_;
  ApplicationLauncherPtr launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationContext);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_APP_APPLICATION_H_
