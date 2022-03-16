// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.inspect/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/syslog/global.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/status.h>

#include "driver_host.h"
#include "src/devices/lib/log/log.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace fdf = fuchsia_driver_framework;
namespace fi = fuchsia::inspect;

constexpr char kDiagnosticsDir[] = "diagnostics";
constexpr size_t kNumDriverLoopThreads = 1;

int main(int argc, char** argv) {
  // TODO(fxbug.dev/33183): Lock down job.
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to redirect stdout to debuglog, assuming test environment and continuing");
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());

  auto serve = outgoing.ServeFromStartupInfo();
  if (serve.is_error()) {
    LOGF(ERROR, "Failed to serve outgoing directory: %s", serve.status_string());
    return serve.status_value();
  }

  // Setup inspect.
  inspect::Inspector inspector;
  if (!inspector) {
    LOGF(ERROR, "Failed to allocate VMO for inspector");
    return ZX_ERR_NO_MEMORY;
  }
  auto tree_handler = inspect::MakeTreeHandler(&inspector, loop.dispatcher());
  auto tree_service = std::make_unique<vfs::Service>(std::move(tree_handler));
  vfs::PseudoDir diagnostics_dir;
  status = diagnostics_dir.AddEntry(fi::Tree::Name_, std::move(tree_service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", fi::Tree::Name_,
         zx_status_get_string(status));
    return status;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  diagnostics_dir.Serve(
      fuchsia_io::wire::kOpenRightWritable | fuchsia_io::wire::kOpenRightReadable |
          fuchsia_io::wire::kOpenRightExecutable | fuchsia_io::wire::kOpenFlagDirectory,
      endpoints->server.TakeChannel(), loop.dispatcher());
  zx::status<> status_result = outgoing.AddDirectory(std::move(endpoints->client), kDiagnosticsDir);
  if (status_result.is_error()) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", kDiagnosticsDir,
         status_result.status_string());
    return status_result.status_value();
  }

  // Setup driver loop.
  async::Loop driver_loop(&kAsyncLoopConfigNeverAttachToThread);
  for (size_t i = 0; i != kNumDriverLoopThreads; ++i) {
    auto thread_name = "driver-loop-" + std::to_string(i);
    status = driver_loop.StartThread(thread_name.data());
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to start thread for driver loop: %s", zx_status_get_string(status));
      return status;
    }
  }

  DriverHost driver_host(inspector, loop, driver_loop.dispatcher());
  auto init = driver_host.PublishDriverHost(outgoing);
  if (init.is_error()) {
    return init.error_value();
  }

  status = loop.Run();

  // We have to shut down the driver_loop before the DriverHost object is freed.
  // Otherwise the loop will continue to access the freed DriverHost.
  driver_loop.Shutdown();

  return status;
}
