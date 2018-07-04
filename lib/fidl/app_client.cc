// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/app_client.h"

#include <fcntl.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/unique_fd.h>
#include <zircon/processargs.h>

namespace modular {
AppClientBase::AppClientBase(fuchsia::sys::Launcher* const launcher,
                             fuchsia::modular::AppConfig config,
                             std::string data_origin,
                             fuchsia::sys::ServiceListPtr additional_services)
    : AsyncHolderBase(config.url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.directory_request = services_.NewRequest();
  launch_info.url = config.url;
  fidl::VectorPtr<fidl::StringPtr> args;
  for (const auto& arg : *config.args) {
    args.push_back(arg);
  }
  launch_info.arguments = std::move(args);

  if (!data_origin.empty()) {
    if (!files::CreateDirectory(data_origin)) {
      FXL_LOG(ERROR) << "Unable to create directory at " << data_origin;
      return;
    }
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/data");

    fxl::UniqueFD dir(open(data_origin.c_str(), O_DIRECTORY | O_RDONLY));
    if (!dir.is_valid()) {
      FXL_LOG(ERROR) << "Unable to open directory at " << data_origin
                     << ". errno: " << errno;
      return;
    }

    launch_info.flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(dir.get()));
    if (!launch_info.flat_namespace->directories->at(0)) {
      FXL_LOG(ERROR) << "Unable create a handle from  " << data_origin;
      return;
    }
  }

  if (additional_services) {
    launch_info.additional_services = std::move(additional_services);
  }
  launcher->CreateComponent(std::move(launch_info), app_.NewRequest());
}

AppClientBase::~AppClientBase() = default;

void AppClientBase::ImplTeardown(std::function<void()> done) {
  ServiceTerminate(std::move(done));
}

void AppClientBase::ImplReset() {
  app_.Unbind();
  ServiceUnbind();
}

void AppClientBase::SetAppErrorHandler(
    const std::function<void()>& error_handler) {
  app_.set_error_handler(error_handler);
}

void AppClientBase::ServiceTerminate(const std::function<void()>& /* done */) {}

void AppClientBase::ServiceUnbind() {}

template <>
void AppClient<fuchsia::modular::Lifecycle>::ServiceTerminate(
    const std::function<void()>& done) {
  SetAppErrorHandler(done);
  if (primary_service())
    primary_service()->Terminate();
}

}  // namespace modular
