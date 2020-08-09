// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/insntrace/ktrace_controller.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/status.h>

#include "garnet/bin/insntrace/utils.h"
#include "garnet/lib/debugger_utils/util.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace insntrace {

constexpr char kKtraceControllerSvc[] = "/svc/fuchsia.tracing.kernel.Controller";
constexpr char kKtraceReaderSvc[] = "/svc/fuchsia.tracing.kernel.Reader";

zx_status_t OpenKtraceControllerChannel(
    fuchsia::tracing::kernel::ControllerSyncPtr* out_controller_ptr) {
  zx_status_t status = fdio_service_connect(
      kKtraceControllerSvc, out_controller_ptr->NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to " << kKtraceControllerSvc << ": " << status;
    return status;
  }
  return status;
}

zx_status_t OpenKtraceReaderChannel(fuchsia::tracing::kernel::ReaderSyncPtr* out_reader_ptr) {
  zx_status_t status =
      fdio_service_connect(kKtraceReaderSvc, out_reader_ptr->NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to " << kKtraceReaderSvc << ": " << status;
    return status;
  }
  return status;
}

bool RequestKtraceStart(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace,
                        uint32_t group_mask) {
  zx_status_t start_status;
  zx_status_t status = ktrace->Start(group_mask, &start_status);
  LogFidlFailure("Ktrace start", status, start_status);
  return status == ZX_OK && start_status == ZX_OK;
}

void RequestKtraceStop(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace) {
  zx_status_t stop_status;
  zx_status_t status = ktrace->Stop(&stop_status);
  LogFidlFailure("Ktrace stop", status, stop_status);
}

void RequestKtraceRewind(const fuchsia::tracing::kernel::ControllerSyncPtr& ktrace) {
  zx_status_t rewind_status;
  zx_status_t status = ktrace->Rewind(&rewind_status);
  LogFidlFailure("Ktrace rewind", status, rewind_status);
}

void DumpKtraceBuffer(const char* output_path_prefix, const char* output_path_suffix) {
  fuchsia::tracing::kernel::ReaderSyncPtr ktrace;
  if (OpenKtraceReaderChannel(&ktrace) != ZX_OK) {
    return;
  }

  std::string ktrace_output_path =
      fxl::StringPrintf("%s.%s", output_path_prefix, output_path_suffix);
  const char* ktrace_c_path = ktrace_output_path.c_str();

  fxl::UniqueFD dest_fd(open(ktrace_c_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR));
  if (dest_fd.is_valid()) {
    std::vector<uint8_t> buf;
    size_t read_size = 1024;
    size_t offset = 0;
    zx_status_t out_status;
    zx_status_t status;
    while ((status = ktrace->ReadAt(read_size, offset, &out_status, &buf)) == ZX_OK &&
           out_status == ZX_OK) {
      if (buf.size() == 0) {
        break;
      }
      size_t bytes_written = write(dest_fd.get(), buf.data(), buf.size());
      if (bytes_written != buf.size()) {
        FX_LOGS(ERROR) << "error writing " << ktrace_c_path;
        break;
      }
      offset += buf.size();
    }
  } else {
    FX_LOGS(ERROR) << fxl::StringPrintf("unable to create %s", ktrace_c_path)
                   << ", errno=" << debugger_utils::ErrnoString(errno);
  }
}

}  // namespace insntrace
