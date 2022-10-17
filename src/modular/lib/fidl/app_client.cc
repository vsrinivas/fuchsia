// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/app_client.h"

#include <fcntl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include <fbl/unique_fd.h>

#include "src/lib/files/directory.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {
AppClientBase::AppClientBase(fuchsia::sys::Launcher* const launcher,
                             fuchsia::modular::session::AppConfig config, std::string data_origin,
                             fuchsia::sys::ServiceListPtr additional_services,
                             fuchsia::sys::FlatNamespacePtr flat_namespace)
    : AsyncHolderBase(config.url()) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.directory_request = services_.NewRequest();
  launch_info.url = config.url();
  std::vector<std::string> args;
  if (config.has_args()) {
    launch_info.arguments.emplace();
    for (const auto& arg : config.args()) {
      launch_info.arguments->push_back(arg);
    }
  }

  if (!data_origin.empty()) {
    if (!files::CreateDirectory(data_origin)) {
      FX_LOGS(ERROR) << "Unable to create directory at " << data_origin;
      return;
    }
    launch_info.flat_namespace = std::make_unique<fuchsia::sys::FlatNamespace>();
    launch_info.flat_namespace->paths.push_back("/data");

    fbl::unique_fd dir(open(data_origin.c_str(), O_DIRECTORY | O_RDONLY));
    if (!dir.is_valid()) {
      FX_LOGS(ERROR) << "Unable to open directory at " << data_origin << ". errno: " << errno;
      return;
    }
    fdio_cpp::FdioCaller caller(std::move(dir));
    zx::result channel = caller.take_directory();
    if (channel.is_error()) {
      FX_PLOGS(ERROR, channel.status_value()) << "Unable create a handle from  " << data_origin;
      return;
    }

    launch_info.flat_namespace->directories.emplace_back(channel.value().TakeChannel());
  }

  if (additional_services) {
    launch_info.additional_services = std::move(additional_services);
  }

  if (flat_namespace) {
    if (!launch_info.flat_namespace) {
      launch_info.flat_namespace = std::make_unique<fuchsia::sys::FlatNamespace>();
    }

    for (size_t i = 0; i < flat_namespace->paths.size(); ++i) {
      launch_info.flat_namespace->paths.push_back(flat_namespace->paths[i]);
      launch_info.flat_namespace->directories.push_back(std::move(flat_namespace->directories[i]));
    }
  }

  launcher->CreateComponent(std::move(launch_info), component_controller_.NewRequest());
}

AppClientBase::~AppClientBase() = default;

void AppClientBase::ImplTeardown(fit::function<void()> done) {
  // If the component is not running, it's not serving the lifecycle service, so don't try to
  // teardown gracefully.
  if (!component_controller_) {
    done();
    return;
  }
  LifecycleServiceTerminate(std::move(done));
}

void AppClientBase::ImplReset() {
  component_controller_.Unbind();
  UnbindLifecycleService();
}

void AppClientBase::SetAppErrorHandler(fit::function<void()> error_handler) {
  component_controller_.set_error_handler(
      [error_handler = std::move(error_handler)](zx_status_t status) { error_handler(); });
}

void AppClientBase::LifecycleServiceTerminate(fit::function<void()> /* done */) {}

void AppClientBase::UnbindLifecycleService() {}

template <>
void AppClient<fuchsia::modular::Lifecycle>::LifecycleServiceTerminate(fit::function<void()> done) {
  if (lifecycle_service()) {
    SetAppErrorHandler(std::move(done));
    lifecycle_service()->Terminate();
  } else {
    // If the lifecycle channel is already closed, the component has no way to receive
    // a terminate signal, so don't bother waiting.
    done();
  }
}

}  // namespace modular
