// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/processargs.h>
#include <mxio/util.h>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"

namespace modular {

ApplicationContext::ApplicationContext(
    fidl::InterfaceHandle<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ServiceProvider> outgoing_services)
    : environment_(ApplicationEnvironmentPtr::Create(std::move(environment))),
      outgoing_services_(std::move(outgoing_services)) {
  if (environment_) {
    environment_->GetServices(environment_services_.NewRequest());
    environment_->GetApplicationLauncher(launcher_.NewRequest());
  }
}

ApplicationContext::~ApplicationContext() = default;

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfo() {
  mx_handle_t environment =
      mxio_get_startup_handle(MX_HND_TYPE_APPLICATION_ENVIRONMENT);
  mx_handle_t services =
      mxio_get_startup_handle(MX_HND_TYPE_APPLICATION_SERVICES);

  return std::make_unique<ApplicationContext>(
      fidl::InterfaceHandle<ApplicationEnvironment>(mx::channel(environment),
                                                    0u),
      fidl::InterfaceRequest<ServiceProvider>(mx::channel(services)));
}

}  // namespace modular
