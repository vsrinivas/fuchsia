// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/responding_service_host.h"

#include "application/lib/app/connect.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace netconnector {

RespondingServiceHost::RespondingServiceHost(
    const modular::ApplicationEnvironmentPtr& environment) {
  FTL_DCHECK(environment);
  environment->GetApplicationLauncher(launcher_.NewRequest());
}

RespondingServiceHost::~RespondingServiceHost() {}

void RespondingServiceHost::RegisterSingleton(
    const std::string& service_name,
    modular::ApplicationLaunchInfoPtr launch_info) {
  service_provider_.AddServiceForName(
      ftl::MakeCopyable([
        this, service_name, launch_info = std::move(launch_info),
        controller = modular::ApplicationControllerPtr()
      ](mx::channel client_handle) mutable {
        FTL_VLOG(2) << "Servicing singleton service request for "
                    << service_name;

        auto iter = service_providers_by_name_.find(service_name);

        if (iter == service_providers_by_name_.end()) {
          FTL_VLOG(1) << "Starting singleton " << launch_info->url
                      << " for service " << service_name;

          // TODO(dalesat): Create application-specific environment.
          // We're launching this application in the environment supplied to
          // the constructor. Instead, we should be launching it in a new
          // environment that is restricted based on app permissions.

          auto dup_launch_info = modular::ApplicationLaunchInfo::New();
          dup_launch_info->url = launch_info->url;
          dup_launch_info->arguments = launch_info->arguments.Clone();
          modular::ServiceProviderPtr service_provider;
          dup_launch_info->services = service_provider.NewRequest();

          launcher_->CreateApplication(std::move(dup_launch_info),
                                       controller.NewRequest());

          service_provider.set_connection_error_handler(
              [this, service_name, &controller] {
                FTL_LOG(ERROR) << "Singleton " << service_name << " died";
                controller.reset();  // kills the singleton application
                service_providers_by_name_.erase(service_name);
              });

          std::tie(iter, std::ignore) = service_providers_by_name_.emplace(
              service_name, std::move(service_provider));
        }

        iter->second->ConnectToService(service_name, std::move(client_handle));
      }),
      service_name);
}

void RespondingServiceHost::RegisterProvider(
    const std::string& service_name,
    fidl::InterfaceHandle<modular::ServiceProvider> handle) {
  modular::ServiceProviderPtr service_provider =
      modular::ServiceProviderPtr::Create(std::move(handle));

  service_provider.set_connection_error_handler([this, service_name] {
    FTL_LOG(ERROR) << "Singleton " << service_name << " provider died";
    service_providers_by_name_.erase(service_name);
  });

  service_providers_by_name_.emplace(service_name, std::move(service_provider));

  service_provider_.AddServiceForName(
      [this, service_name](mx::channel client_handle) {
        FTL_VLOG(2) << "Servicing provided service request for "
                    << service_name;
        auto iter = service_providers_by_name_.find(service_name);
        FTL_DCHECK(iter != service_providers_by_name_.end());
        iter->second->ConnectToService(service_name, std::move(client_handle));
      },
      service_name);
}

}  // namespace netconnector
