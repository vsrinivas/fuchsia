// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/insntrace/ktrace_controller.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <lib/fdio/directory.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/channel.h>
#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/status.h>

#include "garnet/lib/debugger_utils/util.h"

namespace insntrace {

constexpr char kKtraceDevicePath[] = "/dev/misc/ktrace";

bool OpenKtraceChannel(fuchsia::tracing::kernel::ControllerSyncPtr* out_controller_ptr) {
  zx_status_t status = fdio_service_connect(
      kKtraceDevicePath, out_controller_ptr->NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error connecting to " << kKtraceDevicePath << ": " << status;
    return false;
  }
  return true;
}

static void LogFidlFailure(const char* rqst_name, zx_status_t fidl_status,
                           zx_status_t rqst_status) {
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Ktrace FIDL " << rqst_name << " failed: status=" << fidl_status;
  } else if (rqst_status != ZX_OK) {
    FXL_LOG(ERROR) << "Ktrace " << rqst_name << " failed: error=" << rqst_status;
  }
}

bool RequestKtraceStart(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace,
                        uint32_t group_mask) {
  zx_status_t start_status;
  zx_status_t status = ktrace->Start(group_mask, &start_status);
  LogFidlFailure("start", status, start_status);
  return status == ZX_OK && start_status == ZX_OK;
}

void RequestKtraceStop(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace) {
  zx_status_t stop_status;
  zx_status_t status = ktrace->Stop(&stop_status);
  LogFidlFailure("stop", status, stop_status);
}

void RequestKtraceRewind(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace) {
  zx_status_t rewind_status;
  zx_status_t status = ktrace->Rewind(&rewind_status);
  LogFidlFailure("rewind", status, rewind_status);
}

void DumpKtraceBuffer(const char* output_path_prefix, const char* output_path_suffix) {
  int fd = open(kKtraceDevicePath, O_RDONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "open ktrace"
                   << ", " << debugger_utils::ErrnoString(errno);
    return;
  }
  fbl::unique_fd ktrace_fd{fd};

  std::string ktrace_output_path =
      fxl::StringPrintf("%s.%s", output_path_prefix, output_path_suffix);
  const char* ktrace_c_path = ktrace_output_path.c_str();

  fbl::unique_fd dest_fd(open(ktrace_c_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR));
  if (dest_fd.is_valid()) {
    ssize_t count;
    char buf[1024];
    while ((count = read(ktrace_fd.get(), buf, sizeof(buf))) != 0) {
      if (write(dest_fd.get(), buf, count) != count) {
        FXL_LOG(ERROR) << "error writing " << ktrace_c_path;
        break;
      }
    }
  } else {
    FXL_LOG(ERROR) << fxl::StringPrintf("unable to create %s", ktrace_c_path)
                   << ", errno=" << debugger_utils::ErrnoString(errno);
  }
}

}  // namespace insntrace
