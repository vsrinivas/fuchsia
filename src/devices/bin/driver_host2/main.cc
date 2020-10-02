// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/svc/outgoing.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include "driver_host.h"
#include "src/devices/lib/log/log.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace fdf = llcpp::fuchsia::driver::framework;

int main(int argc, char** argv) {
  // TODO(fxbug.dev/33183): Lock down job.
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    LOGF(INFO, "Failed to redirect stdout to debuglog, assuming test environment and continuing");
  }
  // TODO(fxbug.dev/33183): Wrap in stdout-to-debuglog flag check
  status = log_to_debuglog();
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to reconfigure logger to use debuglog: %s", zx_status_get_string(status));
    return status;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  svc::Outgoing outgoing(loop.dispatcher());
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to serve outgoing directory: %s", zx_status_get_string(status));
    return status;
  }

  DriverHost driver_host(&loop);
  auto init = driver_host.PublishDriverHost(outgoing.svc_dir());
  if (init.is_error()) {
    return init.error_value();
  }

  return loop.Run();
}
