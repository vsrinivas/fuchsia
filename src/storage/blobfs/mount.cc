// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/mount.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <memory>
#include <utility>

#include "src/storage/blobfs/component_runner.h"
#include "src/storage/blobfs/runner.h"

namespace blobfs {

zx_status_t Mount(std::unique_ptr<BlockDevice> device, const MountOptions& options,
                  fidl::ServerEnd<fuchsia_io::Directory> root, zx::resource vmex_resource) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  auto runner_or = Runner::Create(&loop, std::move(device), options, std::move(vmex_resource));
  if (runner_or.is_error())
    return runner_or.error_value();

  if (zx_status_t status = runner_or.value()->ServeRoot(std::move(root)); status != ZX_OK) {
    return status;
  }
  loop.Run();
  return ZX_OK;
}

zx::status<> StartComponent(ComponentOptions options, fidl::ServerEnd<fuchsia_io::Directory> root,
                            fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle,
                            zx::resource vmex_resource) {
  // When the loop is destroyed, it can make calls into runner, so runner *must* be destroyed after
  // the loop.
  std::unique_ptr<ComponentRunner> runner;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio provider(loop.dispatcher());

  auto client_end = service::Connect<fuchsia_device_manager::Administrator>();
  if (!client_end.is_ok()) {
    FX_LOGS(WARNING) << "Failed to connect to device manager: " << client_end.status_string()
                     << ". Assuming test environment and continuing";
  }

  runner = std::make_unique<ComponentRunner>(loop, options);
  auto status = runner->ServeRoot(std::move(root), std::move(lifecycle), std::move(*client_end),
                                  std::move(vmex_resource));
  if (status.is_error()) {
    return status;
  }

  loop.Run();

  return zx::ok();
}

}  // namespace blobfs
