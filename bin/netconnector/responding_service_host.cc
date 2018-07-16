// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/responding_service_host.h"

#include "lib/component/cpp/connect.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace netconnector {

RespondingServiceHost::RespondingServiceHost(
    const fuchsia::sys::EnvironmentPtr& environment) {
  FXL_DCHECK(environment);
  environment->GetLauncher(launcher_.NewRequest());
}

RespondingServiceHost::~RespondingServiceHost() {}

void RespondingServiceHost::RegisterSingleton(
    const std::string& service_name, fuchsia::sys::LaunchInfoPtr launch_info) {
  service_namespace_.AddServiceForName(
      fxl::MakeCopyable([this, service_name,
                         launch_info = std::move(launch_info)](
                            zx::channel client_handle) mutable {
        FXL_VLOG(2) << "Handling request for service " << service_name;

        auto iter = service_providers_by_name_.find(service_name);

        if (iter == service_providers_by_name_.end()) {
          FXL_VLOG(1) << "Launching " << launch_info->url << " for service "
                      << service_name;

          // TODO(dalesat): Create application-specific environment.
          // We're launching this application in the environment supplied to
          // the constructor. Instead, we should be launching it in a new
          // environment that is restricted based on app permissions.

          fuchsia::sys::LaunchInfo dup_launch_info;
          dup_launch_info.url = launch_info->url;
          fidl::Clone(launch_info->arguments, &dup_launch_info.arguments);
          component::Services services;
          dup_launch_info.directory_request = services.NewRequest();

          fuchsia::sys::ComponentControllerPtr controller;
          launcher_->CreateComponent(std::move(dup_launch_info),
                                     controller.NewRequest());

          controller.set_error_handler([this, service_name] {
            FXL_LOG(INFO) << "Service " << service_name
                          << " provider disconnected";
            service_providers_by_name_.erase(service_name);
          });

          std::tie(iter, std::ignore) = service_providers_by_name_.emplace(
              std::make_pair<const std::string&, ServicesHolder>(
                  service_name, {std::move(services), std::move(controller)}));
        }

        iter->second.ConnectToService(service_name, std::move(client_handle));
      }),
      service_name);
}

void RespondingServiceHost::RegisterProvider(
    const std::string& service_name,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle) {
  fuchsia::sys::ServiceProviderPtr service_provider = handle.Bind();

  service_provider.set_error_handler([this, service_name] {
    FXL_LOG(INFO) << "Service " << service_name << " provider disconnected";
    service_providers_by_name_.erase(service_name);
  });

  service_providers_by_name_.emplace(service_name, std::move(service_provider));

  service_namespace_.AddServiceForName(
      [this, service_name](zx::channel client_handle) {
        FXL_VLOG(2) << "Servicing provided service request for "
                    << service_name;
        auto iter = service_providers_by_name_.find(service_name);
        FXL_DCHECK(iter != service_providers_by_name_.end());
        iter->second.ConnectToService(service_name, std::move(client_handle));
      },
      service_name);
}

void RespondingServiceHost::ServicesHolder::ConnectToService(
    const std::string& service_name, zx::channel c) {
  if (is_service_provider_) {
    service_provider_->ConnectToService(service_name, std::move(c));
    return;
  }

  services_.ConnectToService(std::move(c), service_name);
}

}  // namespace netconnector
