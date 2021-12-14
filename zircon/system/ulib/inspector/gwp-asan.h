// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_INSPECTOR_GWP_ASAN_H_
#define ZIRCON_SYSTEM_ULIB_INSPECTOR_GWP_ASAN_H_

#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include <vector>

namespace inspector {

struct GwpAsanInfo {
  // Human-readable string about the error. nullptr means there's no GWP-ASan error.
  const char* error_type = nullptr;
  // The access address that causes the exception.
  uintptr_t faulting_addr = 0;
  // The address of the allocation.
  uintptr_t allocation_address;
  // The size of the allocation.
  size_t allocation_size;
  // The allocation trace if there's an error.
  std::vector<uintptr_t> allocation_trace;
  // The free trace if there's an error and the allocation is freed.
  std::vector<uintptr_t> deallocation_trace;
};

// Get the GWP-ASan info from the given process and thread.
//
// Returns a boolean indicating whether the read is successful. If it returns true, |info| is filled
// with the appropriate information. If it returns false, possibilities are
//   * the process is not available for read.
//   * there's no libc.so, or no GWP-ASan note in the libc.so.
//   * GWP-ASan is not enabled.
bool inspector_get_gwp_asan_info(const zx::process& process,
                                 const zx_exception_report_t& exception_report, GwpAsanInfo* info);

}  // namespace inspector

#endif  // ZIRCON_SYSTEM_ULIB_INSPECTOR_GWP_ASAN_H_
