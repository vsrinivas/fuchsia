// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"

#include <async/default.h>
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
    zx::channel service_request)
    : vfs_(async_get_default()),
      export_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      public_export_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      debug_export_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      ctrl_export_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      service_root_(std::move(service_root)),
      deprecated_outgoing_services_(export_dir_) {
  ConnectToEnvironmentService(environment_.NewRequest());
  ConnectToEnvironmentService(launcher_.NewRequest());

  export_dir()->AddEntry("public", public_export_dir_);
  export_dir()->AddEntry("debug", debug_export_dir_);
  export_dir()->AddEntry("ctrl", ctrl_export_dir_);
  if (service_request.is_valid())
    vfs_.ServeDirectory(export_dir_, std::move(service_request));
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
  return std::make_unique<ApplicationContext>(
      subtle::CreateStaticServiceRootHandle(), zx::channel(service_request));
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
      std::move(service_root),
      std::move(startup_info->launch_info->service_request));
}

void ApplicationContext::ConnectToEnvironmentService(
    const std::string& interface_name,
    zx::channel channel) {
  fdio_service_connect_at(service_root_.get(), interface_name.c_str(),
                          channel.release());
}

}  // namespace app
