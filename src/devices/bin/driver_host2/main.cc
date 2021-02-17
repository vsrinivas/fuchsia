// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include "driver_host.h"
#include "src/devices/lib/log/log.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace fdf = llcpp::fuchsia::driver::framework;

constexpr char kDiagnosticsDir[] = "diagnostics";

int main(int argc, char** argv) {
  // TODO(fxbug.dev/33183): Lock down job.
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to redirect stdout to debuglog, assuming test environment and continuing");
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

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
      [tree_handler = std::move(tree_handler)](zx::channel request) {
        tree_handler(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(request)));
        return ZX_OK;
      });
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  status = diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, std::move(tree_service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", fuchsia::inspect::Tree::Name_,
         zx_status_get_string(status));
    return status;
  }
  status = outgoing.root_dir()->AddEntry(kDiagnosticsDir, std::move(diagnostics_dir));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", kDiagnosticsDir,
         zx_status_get_string(status));
    return status;
  }

  DriverHost driver_host(&inspector, &loop);
  auto init = driver_host.PublishDriverHost(outgoing.svc_dir());
  if (init.is_error()) {
    return init.error_value();
  }

  return loop.Run();
}
