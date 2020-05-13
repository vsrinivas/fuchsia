// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/sysinfo.h"

#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include "src/lib/files/unique_fd.h"

namespace debugger_utils {

const char kRootJobSvc[] = "/svc/fuchsia.boot.RootJob";

// TODO(dje): Copied from bin/debug_agent/system_info.cc.
// This is based on the code in Zircon's task-utils which uses this hack to
// get the root job handle. It will likely need to be updated when a better
// way to get the root job is found.
zx::job GetRootJob() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "unable to create channel";
    return zx::job();
  }

  status = fdio_service_connect(kRootJobSvc, remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "unable to open fuchsia.boot.RootJob channel";
    return zx::job();
  }

  zx_handle_t root_job;
  zx_status_t fidl_status = fuchsia_boot_RootJobGet(local.get(), &root_job);
  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "unable to get root job " << fidl_status;
    return zx::job();
  }

  return zx::job(root_job);
}

}  // namespace debugger_utils
