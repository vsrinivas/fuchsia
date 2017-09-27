// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_SCOPE_H_
#define PERIDOT_LIB_FIDL_SCOPE_H_

#include <memory>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/service_provider_impl.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/app/fidl/application_environment_host.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

// A simple implementation of the ApplicationEnvironmentHost that provides fate
// separation of sets of applications run by one application. The environment
// services are delegated to the parent environment.
class Scope : public app::ApplicationEnvironmentHost {
 public:
  Scope(const app::ApplicationEnvironmentPtr& parent_env,
        const std::string& label);

  Scope(const Scope* parent_scope, const std::string& label);

  template <typename Interface>
  void AddService(
      app::ServiceProviderImpl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) {
    service_provider_impl_.AddService(handler, service_name);
  }

  app::ApplicationLauncher* GetLauncher();

  const app::ApplicationEnvironmentPtr& environment() const { return env_; }

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<app::ServiceProvider> environment_services)
      override;

  void InitScope(const app::ApplicationEnvironmentPtr& parent_env,
                 const std::string& label);

  fidl::Binding<app::ApplicationEnvironmentHost> binding_;
  app::ApplicationEnvironmentPtr env_;
  app::ApplicationLauncherPtr env_launcher_;
  app::ApplicationEnvironmentControllerPtr env_controller_;
  app::ServiceProviderImpl service_provider_impl_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_SCOPE_H_
