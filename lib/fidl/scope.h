// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SCOPE_H_
#define APPS_MODULAR_LIB_FIDL_SCOPE_H_

#include <memory>
#include <string>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/modular/services/application/application_environment_host.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

// A simple implementation of the ApplicationEnvironmentHost that provides fate
// separation of sets of applications run by one application. The environment
// services are delegated to the parent environment.
class Scope : public ApplicationEnvironmentHost {
 public:
  Scope(ApplicationEnvironmentPtr parent_env, const std::string& label)
      : binding_(this), parent_env_(std::move(parent_env)) {
    ServiceProviderPtr parent_env_service_provider;
    parent_env_->GetServices(parent_env_service_provider.NewRequest());
    service_provider_impl_.SetDefaultServiceProvider(
        std::move(parent_env_service_provider));
    parent_env_->CreateNestedEnvironment(binding_.NewBinding(),
                                         env_.NewRequest(),
                                         env_controller_.NewRequest(), label);
  }

  template <typename Interface>
  void AddService(
      ServiceProviderImpl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) {
    service_provider_impl_.AddService(handler, service_name);
  }

  ApplicationEnvironmentPtr& environment() { return env_; }

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<ServiceProvider> environment_services) override {
    service_provider_impl_.AddBinding(std::move(environment_services));
  }

  fidl::Binding<ApplicationEnvironmentHost> binding_;
  ApplicationEnvironmentPtr parent_env_;
  ApplicationEnvironmentPtr env_;
  ApplicationEnvironmentControllerPtr env_controller_;
  ServiceProviderImpl service_provider_impl_;
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SCOPE_H_
