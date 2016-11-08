// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/root_environment_host.h"

#include <utility>

#include "apps/modular/services/application/application_environment.fidl.h"

namespace modular {

RootEnvironmentHost::RootEnvironmentHost() : host_binding_(this) {
  fidl::InterfaceHandle<ApplicationEnvironmentHost> host;
  host_binding_.Bind(&host);
  environment_ =
      std::make_unique<ApplicationEnvironmentImpl>(nullptr, std::move(host));
}

RootEnvironmentHost::~RootEnvironmentHost() = default;

void RootEnvironmentHost::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<ServiceProvider> environment_services) {
  service_provider_bindings_.AddBinding(this, std::move(environment_services));
}

void RootEnvironmentHost::ConnectToService(const fidl::String& interface_name,
                                           mx::channel channel) {
  if (interface_name == ApplicationEnvironment::Name_) {
    environment_->Duplicate(
        fidl::InterfaceRequest<ApplicationEnvironment>(std::move(channel)));
  }
}

}  // namespace modular
