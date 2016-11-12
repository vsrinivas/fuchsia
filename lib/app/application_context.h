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

// Provides access to the application's environment and allows the application
// to publish outgoing services back to its creator.
class ApplicationContext {
 public:
  ApplicationContext(fidl::InterfaceHandle<ApplicationEnvironment> environment,
                     fidl::InterfaceRequest<ServiceProvider> outgoing_services);

  ~ApplicationContext();

  // Creates the application context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the application by the application
  // manager.
  //
  // The result is never null.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

  // Gets the application's environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationEnvironmentPtr& environment() const { return environment_; }

  // Gets incoming services provided to the application by the host of
  // its environment.
  //
  // May be null if the application does not have access to its environment.
  const ServiceProviderPtr& environment_services() const {
    return environment_services_;
  }

  // Gets the application launcher service provided to the application by
  // its environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationLauncherPtr& launcher() const { return launcher_; }

  // Gets a service provider implementation by which the application can
  // provide outgoing services back to its creator.
  ServiceProviderImpl* outgoing_services() { return &outgoing_services_; }

  // Connects to a service provided by the application's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> interface_ptr;
    environment_services_->ConnectToService(
        interface_name, GetProxy(&interface_ptr).PassMessagePipe());
    return interface_ptr;
  }

  // Connects to a service provided by the application's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> interface_request,
      const std::string& interface_name = Interface::Name_) {
    environment_services_->ConnectToService(
        interface_name, interface_request.PassMessagePipe());
  }

 private:
  ApplicationEnvironmentPtr environment_;
  ServiceProviderImpl outgoing_services_;
  ServiceProviderPtr environment_services_;
  ApplicationLauncherPtr launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationContext);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_APP_APPLICATION_H_
