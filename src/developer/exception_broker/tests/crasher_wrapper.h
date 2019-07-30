// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

namespace fuchsia {
namespace exception {

// This struct represents all the state needed to keep correct track of an exception.
// It has the owning job and process from the exception.
// The thread can be obtained from the exception if needed.
struct ProcessException {
  zx::job job;
  zx::port port;
  zx::channel exception_channel;

  zx::process process;

  zx::exception exception;
  zx_exception_info_t exception_info;
};

// Spawns a process that will crash and waits for the exception.
// Returns |true| if the process was crashed and the exception was retrieved successfully.
bool SpawnCrasher(ProcessException* pe);

}  // namespace exception
}  // namespace fuchsia
