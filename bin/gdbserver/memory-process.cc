// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory-process.h"

#include <cinttypes>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "process.h"
#include "util.h"

namespace debugserver {

ProcessMemory::ProcessMemory(Process* process) : process_(process) {}

bool ProcessMemory::Read(uintptr_t address,
                         void* out_buffer,
                         size_t length) const {
  FTL_DCHECK(out_buffer);

  mx_handle_t handle = process_->handle();
  FTL_DCHECK(handle != MX_HANDLE_INVALID);

  size_t bytes_read;
  mx_status_t status =
      mx_process_read_memory(handle, address, out_buffer, length, &bytes_read);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus(
        ftl::StringPrintf("Failed to read memory at addr: %" PRIxPTR, address),
        status);
    return false;
  }

  // TODO(dje): The kernel currently doesn't support short reads,
  // despite claims to the contrary.
  FTL_DCHECK(length == bytes_read);

  // TODO(dje): Dump the bytes read at sufficiently high logging level (>2).

  return true;
}

bool ProcessMemory::Write(uintptr_t address,
                          const void* buffer,
                          size_t length) const {
  FTL_DCHECK(buffer);

  // We could be trying to remove a breakpoint after the process has exited.
  // So if the process is gone just return.
  mx_handle_t handle = process_->handle();
  if (handle == MX_HANDLE_INVALID) {
    FTL_VLOG(2) << "No process memory to write to";
    return false;
  }

  if (length == 0) {
    FTL_VLOG(2) << "No data to write";
    return true;
  }

  size_t bytes_written;
  mx_status_t status =
      mx_process_write_memory(handle, address, buffer, length, &bytes_written);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus(
        ftl::StringPrintf("Failed to write memory at addr: %" PRIxPTR, address),
        status);
    return false;
  }

  // TODO(dje): The kernel currently doesn't support short writes,
  // despite claims to the contrary.
  FTL_DCHECK(length == bytes_written);

  // TODO(dje): Dump the bytes written at sufficiently high logging level (>2).

  return true;
}

}  // namespace debugserver
