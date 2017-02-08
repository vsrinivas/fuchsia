// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/application_environment_host_impl.h"

#include "application/lib/app/connect.h"
#include "application/services/application_loader.fidl.h"

namespace maxwell {

ApplicationEnvironmentHostImpl::ApplicationEnvironmentHostImpl(
    modular::ApplicationEnvironment* parent_env) {
  AddService<modular::ApplicationLoader>(
      [parent_env](fidl::InterfaceRequest<modular::ApplicationLoader> request) {
        modular::ServiceProviderPtr root_services;
        parent_env->GetServices(root_services.NewRequest());
        modular::ConnectToService(root_services.get(), std::move(request));
      });
}

void ApplicationEnvironmentHostImpl::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<modular::ServiceProvider> environment_services) {
  AddBinding(std::move(environment_services));
}

}  // namespace maxwell
