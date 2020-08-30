// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bind_devfs_to_namespace.h"

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

namespace isolated_devmgr {

zx::status<> OneTimeSetUp() {
  static zx::status<> status = []() -> zx::status<> {
    // Mark this process as critical so that if this process terminates, all other processes
    // within this job get terminated (e.g. file system processes).
    auto status = zx::make_status(zx::job::default_job()->set_critical(0, *zx::process::self()));
    if (status.is_error()) {
      FX_LOGS(ERROR) << "Unable to make process critical: " << status.status_string();
      return status;
    }
    status = isolated_devmgr::BindDevfsToNamespace();
    if (status.is_error()) {
      FX_LOGS(ERROR) << "Unable to bind devfs to namespace: " << status.status_string();
      return status;
    }
    return zx::ok();
  }();
  return status;
}

zx::status<> BindDevfsToNamespace() {
  fdio_ns_t* name_space;
  auto status = zx::make_status(fdio_ns_get_installed(&name_space));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Failed to get namespace: " << status.status_string();
    return status;
  }
  fidl::SynchronousInterfacePtr<fuchsia::sys2::Realm> realm;
  std::string service_name = std::string("/svc/") + fuchsia::sys2::Realm::Name_;
  status = zx::make_status(
      fdio_service_connect(service_name.c_str(), realm.NewRequest().TakeChannel().get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to realm service: " << status.status_string();
    return status;
  }
  fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir;
  fuchsia::sys2::Realm_BindChild_Result result;
  status = zx::make_status(realm->BindChild(fuchsia::sys2::ChildRef{.name = "isolated_devmgr"},
                                            exposed_dir.NewRequest(), &result));
  if (status.is_error() || result.is_err()) {
    FX_LOGS(ERROR) << "Unable to connect to child: " << status.status_string();
    return status;
  }
  zx::channel handle, request;
  status = zx::make_status(zx::channel::create(0, &handle, &request));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to create channel: " << status.status_string();
    return status;
  }
  status = zx::make_status(
      exposed_dir->Open(fuchsia::io::OPEN_FLAG_DIRECTORY | fuchsia::io::OPEN_RIGHT_READABLE,
                        fuchsia::io::MODE_TYPE_DIRECTORY, "dev",
                        fidl::InterfaceRequest<fuchsia::io::Node>(std::move(request))));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to open dev in child: " << status.status_string();
    return status;
  }
  status = zx::make_status(fdio_ns_bind(name_space, "/dev", handle.release()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Failed to bind /dev to namespace: " << status.status_string();
    return status;
  }
  return zx::ok();
}

}  // namespace isolated_devmgr
