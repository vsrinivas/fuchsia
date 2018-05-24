// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_APPLICATION_CONTEXT_H_
#define LIB_APP_CPP_APPLICATION_CONTEXT_H_

#include <component/cpp/fidl.h>

#include <memory>

#include "lib/app/cpp/outgoing.h"
#include "lib/app/cpp/service_provider_impl.h"
#include "lib/svc/cpp/services.h"

namespace component {

// Provides access to the application's environment and allows the application
// to publish outgoing services back to its creator.
class ApplicationContext {
 public:
  // The constructor is normally called by CreateFromStartupInfo().
  ApplicationContext(zx::channel service_root, zx::channel directory_request);

  ~ApplicationContext();

  ApplicationContext(const ApplicationContext&) = delete;
  ApplicationContext& operator=(const ApplicationContext&) = delete;

  // Creates the application context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the application by the application
  // manager.
  //
  // This function will call FXL_CHECK and stack dump if the environment is
  // null. However, a null environment services pointer is allowed.
  //
  // The returned unique_ptr is never null.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

  // Like CreateFromStartupInfo(), but allows both the environment and the
  // environment services to be null so that callers can validate the values
  // and provide meaningful error messages.
  static std::unique_ptr<ApplicationContext> CreateFromStartupInfoNotChecked();

  static std::unique_ptr<ApplicationContext> CreateFrom(
      StartupInfo startup_info);

  // Gets the application's environment.
  //
  // May be null if the application does not have access to its environment.
  const EnvironmentPtr& environment() const { return environment_; }

  // Whether this application was given services by its environment.
  bool has_environment_services() const {
    return !!incoming_services().directory();
  }

  // Gets the application launcher service provided to the application by
  // its environment.
  //
  // May be null if the application does not have access to its environment.
  const ApplicationLauncherPtr& launcher() const { return launcher_; }

  const Services& incoming_services() const { return incoming_services_; }
  const Outgoing& outgoing() const { return outgoing_; }

  // Gets a service provider implementation by which the application can
  // provide outgoing services back to its creator.
  ServiceNamespace* outgoing_services() {
    return outgoing().deprecated_services();
  }

  // Connects to a service provided by the application's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    return incoming_services().ConnectToService<Interface>(interface_name);
  }

  // Connects to a service provided by the application's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& interface_name = Interface::Name_) {
    return incoming_services().ConnectToService(std::move(request),
                                                interface_name);
  }

  // Connects to a service provided by the application's environment,
  // binding the service to a channel.
  void ConnectToEnvironmentService(const std::string& interface_name,
                                   zx::channel channel);

 private:
  Services incoming_services_;
  Outgoing outgoing_;

  EnvironmentPtr environment_;
  ApplicationLauncherPtr launcher_;
};

}  // namespace component

#endif  // LIB_APP_CPP_APPLICATION_CONTEXT_H_
