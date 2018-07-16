// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/startup_context.h"

#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/fxl/logging.h"

namespace component {

namespace {

constexpr char kServiceRootPath[] = "/svc";

}  // namespace

StartupContext::StartupContext(zx::channel service_root,
                               zx::channel directory_request) {
  incoming_services_.Bind(std::move(service_root));
  outgoing_.Serve(std::move(directory_request));

  incoming_services_.ConnectToService(environment_.NewRequest());
  incoming_services_.ConnectToService(launcher_.NewRequest());
}

StartupContext::~StartupContext() = default;

std::unique_ptr<StartupContext> StartupContext::CreateFromStartupInfo() {
  auto startup_info = CreateFromStartupInfoNotChecked();
  FXL_CHECK(startup_info->environment().get() != nullptr)
      << "The Environment is null. If this is expected, use "
         "CreateFromStartupInfoNotChecked() to allow |environment| to be null.";
  return startup_info;
}

std::unique_ptr<StartupContext>
StartupContext::CreateFromStartupInfoNotChecked() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  return std::make_unique<StartupContext>(
      subtle::CreateStaticServiceRootHandle(), zx::channel(directory_request));
}

std::unique_ptr<StartupContext> StartupContext::CreateFrom(
    fuchsia::sys::StartupInfo startup_info) {
  fuchsia::sys::FlatNamespace& flat = startup_info.flat_namespace;
  if (flat.paths->size() != flat.directories->size())
    return nullptr;

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths->size(); ++i) {
    if (flat.paths->at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories->at(i));
      break;
    }
  }

  return std::make_unique<StartupContext>(
      std::move(service_root),
      std::move(startup_info.launch_info.directory_request));
}

void StartupContext::ConnectToEnvironmentService(
    const std::string& interface_name, zx::channel channel) {
  incoming_services().ConnectToService(std::move(channel), interface_name);
}

}  // namespace component
