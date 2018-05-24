// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"

#include <fdio/util.h>
#include <lib/async/default.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/app/cpp/connect.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/logging.h"

namespace component {

namespace {

constexpr char kServiceRootPath[] = "/svc";

}  // namespace

ApplicationContext::ApplicationContext(zx::channel service_root,
                                       zx::channel directory_request) {
  incoming_services_.Bind(std::move(service_root));
  outgoing_.Serve(std::move(directory_request));

  incoming_services_.ConnectToService(environment_.NewRequest());
  incoming_services_.ConnectToService(launcher_.NewRequest());
}

ApplicationContext::~ApplicationContext() = default;

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfo() {
  auto startup_info = CreateFromStartupInfoNotChecked();
  FXL_CHECK(startup_info->environment().get() != nullptr)
      << "The Environment is null. If this is expected, use "
         "CreateFromStartupInfoNotChecked() to allow |environment| to be null.";
  return startup_info;
}

std::unique_ptr<ApplicationContext>
ApplicationContext::CreateFromStartupInfoNotChecked() {
  zx_handle_t directory_request = zx_get_startup_handle(PA_DIRECTORY_REQUEST);
  return std::make_unique<ApplicationContext>(
      subtle::CreateStaticServiceRootHandle(), zx::channel(directory_request));
}

std::unique_ptr<ApplicationContext> ApplicationContext::CreateFrom(
    StartupInfo startup_info) {
  FlatNamespace& flat = startup_info.flat_namespace;
  if (flat.paths->size() != flat.directories->size())
    return nullptr;

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths->size(); ++i) {
    if (flat.paths->at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories->at(i));
      break;
    }
  }

  return std::make_unique<ApplicationContext>(
      std::move(service_root),
      std::move(startup_info.launch_info.directory_request));
}

void ApplicationContext::ConnectToEnvironmentService(
    const std::string& interface_name, zx::channel channel) {
  return incoming_services().ConnectToService(std::move(channel),
                                              interface_name);
}

}  // namespace component
