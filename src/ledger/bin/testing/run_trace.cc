// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/run_trace.h"

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/device/vfs.h>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

constexpr char kTraceUrl[] = "fuchsia-pkg://fuchsia.com/trace#meta/trace.cmx";

void RunTrace(sys::ComponentContext* component_context,
              fuchsia::sys::ComponentControllerPtr* component_controller,
              const std::vector<std::string>& argv) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTraceUrl;
  launch_info.arguments = argv;

  zx::channel dir, server;
  zx_status_t status = zx::channel::create(0, &dir, &server);
  LEDGER_CHECK(status == ZX_OK);
  status = fdio_open(kTraceTestDataLocalPath, ZX_FS_RIGHT_READABLE, server.release());
  LEDGER_CHECK(status == ZX_OK);
  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  launch_info.flat_namespace->paths.push_back(kTraceTestDataRemotePath);
  launch_info.flat_namespace->directories.push_back(std::move(dir));

  fuchsia::sys::LauncherPtr launcher;
  component_context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), component_controller->NewRequest());
}

}  // namespace ledger
