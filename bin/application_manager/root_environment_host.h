// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_ENVIRONMENT_HOST_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_ENVIRONMENT_HOST_H_

#include <memory>

#include "apps/modular/src/application_manager/application_environment_impl.h"
#include "apps/modular/services/application/application_environment_host.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace modular {

class RootEnvironmentHost : public ApplicationEnvironmentHost,
                            public ServiceProvider {
 public:
  RootEnvironmentHost();
  ~RootEnvironmentHost() override;

  ApplicationEnvironmentImpl* environment() const { return environment_.get(); }

  // ApplicationEnvironmentHost implementation:

  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<ServiceProvider> environment_services) override;

  // ServiceProvider implementation:

  void ConnectToService(const fidl::String& interface_name,
                        mx::channel channel) override;

 private:
  fidl::Binding<ApplicationEnvironmentHost> host_binding_;
  fidl::BindingSet<ServiceProvider> service_provider_bindings_;

  std::unique_ptr<ApplicationEnvironmentImpl> environment_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RootEnvironmentHost);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_ENVIRONMENT_HOST_H_
