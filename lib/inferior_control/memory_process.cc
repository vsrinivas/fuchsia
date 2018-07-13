// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_process.h"

#include <zircon/syscalls.h>
#include <cinttypes>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "process.h"

namespace debugserver {

ProcessMemory::ProcessMemory(Process* process) : process_(process) {}

bool ProcessMemory::Read(uintptr_t address, void* out_buffer,
                         size_t length) const {
  FXL_DCHECK(out_buffer);

  zx_handle_t handle = process_->handle();
  FXL_DCHECK(handle != ZX_HANDLE_INVALID);

  size_t bytes_read;
  zx_status_t status =
      zx_process_read_memory(handle, address, out_buffer, length, &bytes_read);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "Failed to read memory at addr: %" PRIxPTR ": ",
                          address)
                   << ZxErrorString(status);
    return false;
  }

  // TODO(dje): The kernel currently doesn't support short reads,
  // despite claims to the contrary.
  FXL_DCHECK(length == bytes_read);

  // TODO(dje): Dump the bytes read at sufficiently high logging level (>2).

  return true;
}

bool ProcessMemory::Write(uintptr_t address, const void* buffer,
                          size_t length) const {
  FXL_DCHECK(buffer);

  // We could be trying to remove a breakpoint after the process has exited.
  // So if the process is gone just return.
  zx_handle_t handle = process_->handle();
  if (handle == ZX_HANDLE_INVALID) {
    FXL_VLOG(2) << "No process memory to write to";
    return false;
  }

  if (length == 0) {
    FXL_VLOG(2) << "No data to write";
    return true;
  }

  size_t bytes_written;
  zx_status_t status =
      zx_process_write_memory(handle, address, buffer, length, &bytes_written);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "Failed to write memory at addr: %" PRIxPTR ": ",
                          address)
                   << ZxErrorString(status);
    return false;
  }

  // TODO(dje): The kernel currently doesn't support short writes,
  // despite claims to the contrary.
  FXL_DCHECK(length == bytes_written);

  // TODO(dje): Dump the bytes written at sufficiently high logging level (>2).

  return true;
}

}  // namespace debugserver
