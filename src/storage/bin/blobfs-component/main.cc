// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include "src/storage/blobfs/mount.h"

zx::resource AttemptToGetVmexResource() {
  auto client_end = service::Connect<fuchsia_kernel::VmexResource>();
  if (!client_end.is_ok()) {
    FX_LOGS(WARNING) << "Failed to connect to fuchsia.kernel.VmexResource: "
                     << client_end.status_string();
    return zx::resource();
  }

  auto result = fidl::WireCall(*client_end)->Get();
  if (!result.ok()) {
    FX_LOGS(WARNING) << "fuchsia.kernel.VmexResource.Get() failed: " << result.status_string();
    return zx::resource();
  }
  return std::move(result->vmex_resource);
}

int main() {
  FX_LOGS(INFO) << "starting blobfs component";

  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!outgoing_server.is_valid()) {
    FX_LOGS(ERROR) << "PA_DIRECTORY_REQUEST startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir(std::move(outgoing_server));

  zx::channel lifecycle_channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
  if (!lifecycle_channel.is_valid()) {
    FX_LOGS(ERROR) << "PA_LIFECYCLE startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request(
      std::move(lifecycle_channel));

  zx::resource vmex = AttemptToGetVmexResource();
  if (!vmex.is_valid()) {
    FX_LOGS(WARNING) << "VMEX resource unavailable, executable blobs are unsupported";
  }

  // blocks until blobfs exits
  zx::status status = blobfs::StartComponent(std::move(outgoing_dir), std::move(lifecycle_request),
                                             std::move(vmex));
  if (status.is_error()) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
