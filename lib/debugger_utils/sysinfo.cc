// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/sysinfo.h"

#include <fcntl.h>
#include <lib/zx/job.h>
#include <unistd.h>

#include <zircon/device/sysinfo.h>
#include <zircon/syscalls.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

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

  zx_handle_t root_job;
  size_t n = ioctl_sysinfo_get_root_job(fd.get(), &root_job);
  if (n != sizeof(root_job)) {
    FXL_LOG(ERROR) << "unable to get root job, bad size returned";
    return zx::job();
  }

  return zx::job(root_job);
}

}  // namespace debugger_utils
