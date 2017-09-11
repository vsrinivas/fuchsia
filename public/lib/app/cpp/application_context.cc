// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <mxio/util.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/ftl/logging.h"

namespace app {
namespace {

constexpr char kServiceRootPath[] = "/svc";

mx::channel GetServiceRoot() {
  mx::channel h1, h2;
  if (mx::channel::create(0, &h1, &h2) != MX_OK)
    return mx::channel();

  // TODO(abarth): Use kServiceRootPath once that actually works.
  if (mxio_service_connect("/svc/.", h1.release()) != MX_OK)
    return mx::channel();

  return h2;
}

}  // namespace

ApplicationContext::ApplicationContext(
    mx::channel service_root,
    mx::channel service_request,
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
  FTL_CHECK(startup_info->environment().get() != nullptr)
      << "The ApplicationEnvironment is null. If this is expected, use "
         "CreateFromStartupInfoNotChecked() to allow |environment| to be null.";
  return startup_info;
}

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfoNotChecked() {
  mx_handle_t service_request = mx_get_startup_handle(PA_SERVICE_REQUEST);
  mx_handle_t services = mx_get_startup_handle(PA_APP_SERVICES);
  return std::make_unique<ApplicationContext>(
      GetServiceRoot(), mx::channel(service_request),
      fidl::InterfaceRequest<ServiceProvider>(mx::channel(services)));
}

std::unique_ptr<ApplicationContext> ApplicationContext::CreateFrom(
    ApplicationStartupInfoPtr startup_info) {
  const FlatNamespacePtr& flat = startup_info->flat_namespace;
  if (flat->paths.size() != flat->directories.size())
    return nullptr;

  mx::channel service_root;
  for (size_t i = 0; i < flat->paths.size(); ++i) {
    if (flat->paths[i] == kServiceRootPath) {
      service_root = std::move(flat->directories[i]);
      break;
    }
  }

  return std::make_unique<ApplicationContext>(
      GetServiceRoot(), std::move(startup_info->launch_info->service_request),
      std::move(startup_info->launch_info->services));
}

void ApplicationContext::ConnectToEnvironmentService(
    const std::string& interface_name,
    mx::channel channel) {
  mxio_service_connect_at(service_root_.get(), interface_name.c_str(),
                          channel.release());
}

}  // namespace app
