// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/app.h"

#include <fcntl.h>
#include <unistd.h>

#include <lib/async/default.h>
#include <trace-engine/instrumentation.h>
#include <trace-provider/provider.h>
#include <zircon/device/ktrace.h>
#include <zircon/syscalls/log.h>

#include "garnet/bin/ktrace_provider/importer.h"
#include "garnet/bin/ktrace_provider/reader.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"

namespace ktrace_provider {
namespace {

constexpr char kKTraceDev[] = "/dev/misc/ktrace";

struct KTraceCategory {
  const char* name;
  uint32_t group;
};

constexpr KTraceCategory kGroupCategories[] = {
    {"kernel", KTRACE_GRP_ALL},
    {"kernel:meta", KTRACE_GRP_META},
    {"kernel:lifecycle", KTRACE_GRP_LIFECYCLE},
    {"kernel:sched", KTRACE_GRP_SCHEDULER},
    {"kernel:tasks", KTRACE_GRP_TASKS},
    {"kernel:ipc", KTRACE_GRP_IPC},
    {"kernel:irq", KTRACE_GRP_IRQ},
    {"kernel:probe", KTRACE_GRP_PROBE},
    {"kernel:arch", KTRACE_GRP_ARCH},
};

constexpr char kLogCategory[] = "log";

fxl::UniqueFD OpenKTrace() {
  int result = open(kKTraceDev, O_WRONLY);
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to open " << kKTraceDev << ": errno=" << errno;
  }
  return fxl::UniqueFD(result);  // take ownership here
}

void IoctlKtraceStop(int fd) {
  zx_status_t status = ioctl_ktrace_stop(fd);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_ktrace_stop failed: status=" << status;
}

void IoctlKtraceStart(int fd, uint32_t group_mask) {
  zx_status_t status = ioctl_ktrace_start(fd, &group_mask);
  if (status != ZX_OK)
    FXL_LOG(ERROR) << "ioctl_ktrace_start failed: status=" << status;
}

}  // namespace

App::App(const fxl::CommandLine& command_line)
    : startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {
  trace_observer_.Start(async_get_default_dispatcher(), [this] { UpdateState(); });
}

App::~App() {}

void App::UpdateState() {
  uint32_t group_mask = 0;
  bool capture_log = false;
  if (trace_state() == TRACE_STARTED) {
    for (size_t i = 0; i < arraysize(kGroupCategories); i++) {
      auto& category = kGroupCategories[i];
      if (trace_is_category_enabled(category.name)) {
        group_mask |= category.group;
      }
    }
    capture_log = trace_is_category_enabled(kLogCategory);
  }

  if (current_group_mask_ != group_mask) {
    StopKTrace();
    StartKTrace(group_mask);
  }

  if (capture_log) {
    log_importer_.Start();
  } else {
    log_importer_.Stop();
  }
}

void App::StartKTrace(uint32_t group_mask) {
  FXL_DCHECK(!context_);
  if (!group_mask) {
    return;  // nothing to trace
  }

  FXL_LOG(INFO) << "Starting ktrace";

  fxl::UniqueFD fd = OpenKTrace();
  if (!fd.is_valid()) {
    return;
  }

  context_ = trace_acquire_context();
  if (!context_) {
    // Tracing was disabled in the meantime.
    return;
  }
  current_group_mask_ = group_mask;

  IoctlKtraceStop(fd.get());
  IoctlKtraceStart(fd.get(), group_mask);

  FXL_LOG(INFO) << "Started ktrace";
}

void App::StopKTrace() {
  if (!context_) {
    return;  // not currently tracing
  }
  FXL_DCHECK(current_group_mask_);

  FXL_LOG(INFO) << "Stopping ktrace";

  fxl::UniqueFD fd = OpenKTrace();
  if (fd.is_valid()) {
    IoctlKtraceStop(fd.get());
  }

  Reader reader;
  Importer importer(context_);
  if (!importer.Import(reader)) {
    FXL_LOG(ERROR) << "Errors encountered while importing ktrace data";
  }

  trace_release_context(context_);
  context_ = nullptr;
  current_group_mask_ = 0u;
}

}  // namespace ktrace_provider
