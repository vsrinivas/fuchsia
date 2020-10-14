// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/app.h"

#include <fcntl.h>
#include <fuchsia/tracing/kernel/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-engine/instrumentation.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

#include <iterator>

#include "garnet/bin/ktrace_provider/device_reader.h"
#include "garnet/bin/ktrace_provider/importer.h"

namespace ktrace_provider {
namespace {

constexpr char kKtraceControllerSvc[] = "/svc/fuchsia.tracing.kernel.Controller";
using fuchsia::tracing::kernel::Controller_SyncProxy;
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
    {"kernel:syscall", KTRACE_GRP_SYSCALL},
    {"kernel:vm", KTRACE_GRP_VM},
};

// Meta category to retain current contents of ktrace buffer.
constexpr char kRetainCategory[] = "kernel:retain";

constexpr char kLogCategory[] = "log";

zx::channel OpenKTraceController() {
  int fd = open(kKtraceControllerSvc, O_WRONLY);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Failed to open " << kKtraceControllerSvc << ": errno=" << errno;
    return zx::channel();
  }
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get " << kKtraceControllerSvc
                   << " channel: " << zx_status_get_string(status);
    return zx::channel();
  }
  return channel;
}

void LogFidlFailure(const char* rqst_name, zx_status_t fidl_status, zx_status_t rqst_status) {
  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "Ktrace FIDL " << rqst_name << " failed: status=" << fidl_status;
  } else if (rqst_status != ZX_OK) {
    FX_LOGS(ERROR) << "Ktrace " << rqst_name << " failed: status=" << rqst_status;
  }
}

void RequestKtraceStop(Controller_SyncProxy& controller) {
  zx_status_t stop_status;
  zx_status_t status = controller.Stop(&stop_status);
  LogFidlFailure("stop", status, stop_status);
}

void RequestKtraceRewind(Controller_SyncProxy& controller) {
  zx_status_t rewind_status;
  zx_status_t status = controller.Rewind(&rewind_status);
  LogFidlFailure("rewind", status, rewind_status);
}

void RequestKtraceStart(Controller_SyncProxy& controller, uint32_t group_mask) {
  zx_status_t start_status;
  zx_status_t status = controller.Start(group_mask, &start_status);
  LogFidlFailure("start", status, start_status);
}

}  // namespace

App::App(const fxl::CommandLine& command_line)
    : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
  trace_observer_.Start(async_get_default_dispatcher(), [this] { UpdateState(); });
}

App::~App() {}

void App::UpdateState() {
  uint32_t group_mask = 0;
  bool capture_log = false;
  bool retain_current_data = false;
  if (trace_state() == TRACE_STARTED) {
    size_t num_enabled_categories = 0;
    for (size_t i = 0; i < std::size(kGroupCategories); i++) {
      auto& category = kGroupCategories[i];
      if (trace_is_category_enabled(category.name)) {
        group_mask |= category.group;
        ++num_enabled_categories;
      }
    }

    // Avoid capturing log traces in the default case by detecting whether all
    // categories are enabled or not.
    capture_log = trace_is_category_enabled(kLogCategory) &&
                  num_enabled_categories != std::size(kGroupCategories);

    // The default case is everything is enabled, but |kRetainCategory| must be
    // explicitly passed.
    retain_current_data = trace_is_category_enabled(kRetainCategory) &&
                          num_enabled_categories != std::size(kGroupCategories);
  }

  if (current_group_mask_ != group_mask) {
    StopKTrace();
    StartKTrace(group_mask, retain_current_data);
  }

  if (capture_log) {
    log_importer_.Start();
  } else {
    log_importer_.Stop();
  }
}

void App::StartKTrace(uint32_t group_mask, bool retain_current_data) {
  FX_DCHECK(!context_);
  if (!group_mask) {
    return;  // nothing to trace
  }

  FX_LOGS(INFO) << "Starting ktrace";

  zx::channel channel = OpenKTraceController();
  if (!channel) {
    return;
  }
  Controller_SyncProxy ktrace_controller(std::move(channel));

  context_ = trace_acquire_prolonged_context();
  if (!context_) {
    // Tracing was disabled in the meantime.
    return;
  }
  current_group_mask_ = group_mask;

  RequestKtraceStop(ktrace_controller);
  if (!retain_current_data) {
    RequestKtraceRewind(ktrace_controller);
  }
  RequestKtraceStart(ktrace_controller, group_mask);

  FX_VLOGS(1) << "Ktrace started";
}

void App::StopKTrace() {
  if (!context_) {
    return;  // not currently tracing
  }
  FX_DCHECK(current_group_mask_);

  FX_LOGS(INFO) << "Stopping ktrace";

  {
    zx::channel channel = OpenKTraceController();
    if (channel) {
      Controller_SyncProxy ktrace_controller(std::move(channel));
      RequestKtraceStop(ktrace_controller);
    }
  }

  // Acquire a context for writing to the trace buffer.
  auto buffer_context = trace_acquire_context();

  DeviceReader reader;
  if (reader.Init() == ZX_OK) {
    Importer importer(buffer_context);
    if (!importer.Import(reader)) {
      FX_LOGS(ERROR) << "Errors encountered while importing ktrace data";
    }
  } else {
    FX_LOGS(ERROR) << "Failed to initialize ktrace reader";
  }

  trace_release_context(buffer_context);
  trace_release_prolonged_context(context_);
  context_ = nullptr;
  current_group_mask_ = 0u;

  FX_VLOGS(1) << "Ktrace stopped";
}

}  // namespace ktrace_provider
