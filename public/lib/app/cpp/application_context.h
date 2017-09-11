// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_APP_APPLICATION_CONTEXT_H_
#define APPLICATION_LIB_APP_APPLICATION_CONTEXT_H_

#include <memory>

#include "application/lib/app/service_provider_impl.h"
#include "application/lib/svc/service_namespace.h"
#include "application/services/application_environment.fidl.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/application_runner.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace app {

// Provides access to the application's environment and allows the application
// to publish outgoing services back to its creator.
class ApplicationContext {
 public:
  // The constructor is normally called by CreateFromStartupInfo().
  ApplicationContext(mx::channel service_root,
                     mx::channel service_request,
                     fidl::InterfaceRequest<ServiceProvider> outgoing_services);

  ~ApplicationContext();

  // Creates the application context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the application by the application
  // manager.
  //
  // This function will call FTL_CHECK and stack dump if the environment is
  // null. However, a null environment services pointer is allowed.
  //
  // The returned unique_ptr is never null.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

  // Like CreateFromStartupInfo(), but allows both the environment and the
  // environment services to be null so that callers can validate the values
  // and provide meaningful error messages.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfoNotChecked();

  static std::unique_ptr<ApplicationContext> CreateFrom(
      ApplicationStartupInfoPtr startup_info);

  // Gets the application's environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationEnvironmentPtr& environment() const { return environment_; }

  // Whether this application was given services by its environment.
  bool has_environment_services() const { return !!service_root_; }

  // Gets the application launcher service provided to the application by
  // its environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationLauncherPtr& launcher() const { return launcher_; }

  // Gets a service provider implementation by which the application can
  // provide outgoing services back to its creator.
  ServiceNamespace* outgoing_services() { return &outgoing_services_; }

  // Connects to a service provided by the application's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> interface_ptr;
    ConnectToEnvironmentService(interface_name,
                                interface_ptr.NewRequest().PassChannel());
    return interface_ptr;
  }

  // Connects to a service provided by the application's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& interface_name = Interface::Name_) {
    ConnectToEnvironmentService(interface_name, request.PassChannel());
  }

  // Connects to a service provided by the application's environment,
  // binding the service to a channel.
  void ConnectToEnvironmentService(const std::string& interface_name,
                                   mx::channel channel);

 private:
  ApplicationEnvironmentPtr environment_;
  ServiceNamespace outgoing_services_;
  mx::channel service_root_;
  ApplicationLauncherPtr launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationContext);
};

}  // namespace app

#endif  // APPLICATION_LIB_APP_APPLICATION_CONTEXT_H_
