// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/sysinfo.h"

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <unistd.h>

#include <fuchsia/sysinfo/c/fidl.h>
#include <zircon/syscalls.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"

namespace debugger_utils {

const char kSysinfoDevice[] = "/dev/misc/sysinfo";

// TODO(dje): Copied from bin/debug_agent/system_info.cc.
// This is based on the code in Zircon's task-utils which uses this hack to
// get the root job handle. It will likely need to be updated when a better
// way to get the root job is found.
zx::job GetRootJob() {
  fxl::UniqueFD fd(open(kSysinfoDevice, O_RDWR));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << "unable to open " << kSysinfoDevice;
    return zx::job();
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "unable to open sysinfo channel";
    return zx::job();
  }

  zx_handle_t root_job;
  zx_status_t fidl_status =
      fuchsia_sysinfo_DeviceGetRootJob(channel.get(), &status, &root_job);
  if (fidl_status != ZX_OK || status != ZX_OK) {
    FXL_LOG(ERROR) << "unable to get root job";
    return zx::job();
  }

  return zx::job(root_job);
}

}  // namespace debugger_utils
