// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/app_client.h"

#include <fcntl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <zircon/processargs.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/syslog/cpp/logger.h"

namespace modular {
AppClientBase::AppClientBase(fuchsia::sys::Launcher* const launcher,
                             fuchsia::modular::AppConfig config, std::string data_origin,
                             fuchsia::sys::ServiceListPtr additional_services,
                             fuchsia::sys::FlatNamespacePtr flat_namespace)
    : AsyncHolderBase(config.url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.directory_request = services_.NewRequest();
  launch_info.url = config.url;
  std::vector<std::string> args;
  if (config.args.has_value()) {
    launch_info.arguments.emplace();
    for (const auto& arg : *config.args) {
      launch_info.arguments->push_back(arg);
    }
  }

  if (!data_origin.empty()) {
    if (!files::CreateDirectory(data_origin)) {
      FX_LOGS(ERROR) << "Unable to create directory at " << data_origin;
      return;
    }
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/data");

    fbl::unique_fd dir(open(data_origin.c_str(), O_DIRECTORY | O_RDONLY));
    if (!dir.is_valid()) {
      FX_LOGS(ERROR) << "Unable to open directory at " << data_origin << ". errno: " << errno;
      return;
    }

    launch_info.flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(dir.get()));
    if (!launch_info.flat_namespace->directories.at(0)) {
      FX_LOGS(ERROR) << "Unable create a handle from  " << data_origin;
      return;
    }
  }

  if (additional_services) {
    launch_info.additional_services = std::move(additional_services);
  }

  if (flat_namespace) {
    if (!launch_info.flat_namespace) {
      launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    }

    for (size_t i = 0; i < flat_namespace->paths.size(); ++i) {
      launch_info.flat_namespace->paths.push_back(flat_namespace->paths[i]);
      launch_info.flat_namespace->directories.push_back(std::move(flat_namespace->directories[i]));
    }
  }

  launcher->CreateComponent(std::move(launch_info), app_.NewRequest());
}

AppClientBase::~AppClientBase() = default;

void AppClientBase::ImplTeardown(fit::function<void()> done) { ServiceTerminate(std::move(done)); }

void AppClientBase::ImplReset() {
  app_.Unbind();
  ServiceUnbind();
}

void AppClientBase::SetAppErrorHandler(fit::function<void()> error_handler) {
  app_.set_error_handler(
      [error_handler = std::move(error_handler)](zx_status_t status) { error_handler(); });
}

void AppClientBase::ServiceTerminate(fit::function<void()> /* done */) {}

void AppClientBase::ServiceUnbind() {}

template <>
void AppClient<fuchsia::modular::Lifecycle>::ServiceTerminate(fit::function<void()> done) {
  SetAppErrorHandler(std::move(done));
  if (primary_service())
    primary_service()->Terminate();
}

}  // namespace modular
