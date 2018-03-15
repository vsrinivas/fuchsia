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
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace modular {

// Provides fate separation of sets of applications run by one application. The
// environment services are delegated to the parent environment.
class Scope {
 public:
  Scope(const app::ApplicationEnvironmentPtr& parent_env,
        const std::string& label);

  Scope(const Scope* parent_scope, const std::string& label);

  template <typename Interface>
  void AddService(
      app::ServiceProviderImpl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) {
    service_provider_bridge_.AddService(handler, service_name);
  }

  app::ApplicationLauncher* GetLauncher();

  const app::ApplicationEnvironmentPtr& environment() const { return env_; }

 private:
  void InitScope(const app::ApplicationEnvironmentPtr& parent_env,
                 const std::string& label);

  app::ServiceProviderBridge service_provider_bridge_;
  app::ApplicationEnvironmentPtr env_;
  app::ApplicationLauncherPtr env_launcher_;
  app::ApplicationEnvironmentControllerPtr env_controller_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_SCOPE_H_
