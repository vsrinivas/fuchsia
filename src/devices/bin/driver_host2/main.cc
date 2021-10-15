// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/global.h>
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

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to serve outgoing directory: %s", zx_status_get_string(status));
    return status;
  }

  // Setup inspect.
  inspect::Inspector inspector;
  if (!inspector) {
    LOGF(ERROR, "Failed to allocate VMO for inspector");
    return ZX_ERR_NO_MEMORY;
  }
  auto tree_handler = inspect::MakeTreeHandler(&inspector, loop.dispatcher());
  auto tree_service = fbl::MakeRefCounted<fs::Service>(
      [tree_handler = std::move(tree_handler)](fidl::ServerEnd<fi::Tree> request) {
        tree_handler(fidl::InterfaceRequest<fi::Tree>(request.TakeChannel()));
        return ZX_OK;
      });
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  status = diagnostics_dir->AddEntry(fi::Tree::Name_, std::move(tree_service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", fi::Tree::Name_,
         zx_status_get_string(status));
    return status;
  }
  status = outgoing.root_dir()->AddEntry(kDiagnosticsDir, std::move(diagnostics_dir));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", kDiagnosticsDir,
         zx_status_get_string(status));
    return status;
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
  auto init = driver_host.PublishDriverHost(outgoing.svc_dir());
  if (init.is_error()) {
    return init.error_value();
  }

  return loop.Run();
}
