// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"

#include <fdio/util.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/logging.h"

namespace app {

namespace {

constexpr char kServiceRootPath[] = "/svc";

}  // namespace

ApplicationContext::ApplicationContext(
    zx::channel service_root,
    zx::channel service_request,
    fidl::InterfaceRequest<ServiceProvider> outgoing_services)
    : outgoing_services_(std::move(outgoing_services)),
      service_root_(std::move(service_root)) {
  ConnectToEnvironmentService(environment_.NewRequest());
  ConnectToEnvironmentService(launcher_.NewRequest());

  if (service_request.is_valid())
    outgoing_services_.ServeDirectory(std::move(service_request));
}

ApplicationContext::~ApplicationContext() = default;

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfo() {
  auto startup_info = CreateFromStartupInfoNotChecked();
  FXL_CHECK(startup_info->environment().get() != nullptr)
      << "The ApplicationEnvironment is null. If this is expected, use "
         "CreateFromStartupInfoNotChecked() to allow |environment| to be null.";
  return startup_info;
}

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfoNotChecked() {
  zx_handle_t service_request = zx_get_startup_handle(PA_SERVICE_REQUEST);
  zx_handle_t services = zx_get_startup_handle(PA_APP_SERVICES);
  return std::make_unique<ApplicationContext>(
      subtle::CreateStaticServiceRootHandle(), zx::channel(service_request),
      fidl::InterfaceRequest<ServiceProvider>(zx::channel(services)));
}

std::unique_ptr<ApplicationContext> ApplicationContext::CreateFrom(
    ApplicationStartupInfoPtr startup_info) {
  const FlatNamespacePtr& flat = startup_info->flat_namespace;
  if (flat->paths.size() != flat->directories.size())
    return nullptr;

  zx::channel service_root;
  for (size_t i = 0; i < flat->paths.size(); ++i) {
    if (flat->paths[i] == kServiceRootPath) {
      service_root = std::move(flat->directories[i]);
      break;
    }
  }

  return std::make_unique<ApplicationContext>(
      subtle::CreateStaticServiceRootHandle(),
      std::move(startup_info->launch_info->service_request),
      std::move(startup_info->launch_info->services));
}

void ApplicationContext::ConnectToEnvironmentService(
    const std::string& interface_name,
    zx::channel channel) {
  fdio_service_connect_at(service_root_.get(), interface_name.c_str(),
                          channel.release());
}

}  // namespace app
