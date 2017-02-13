// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/application_environment_host_impl.h"

#include "application/lib/app/connect.h"
#include "application/services/application_loader.fidl.h"

namespace maxwell {

ApplicationEnvironmentHostImpl::ApplicationEnvironmentHostImpl(
    app::ApplicationEnvironment* parent_env) {
  AddService<app::ApplicationLoader>(
      [parent_env](fidl::InterfaceRequest<app::ApplicationLoader> request) {
        app::ServiceProviderPtr root_services;
        parent_env->GetServices(root_services.NewRequest());
        app::ConnectToService(root_services.get(), std::move(request));
      });
}

void ApplicationEnvironmentHostImpl::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<app::ServiceProvider> environment_services) {
  AddBinding(std::move(environment_services));
}

}  // namespace maxwell
