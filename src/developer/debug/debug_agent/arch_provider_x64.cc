// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_provider_x64.h"

#include <zircon/syscalls/exception.h>

namespace debug_agent {
namespace arch {

void ArchProviderX64::FillExceptionRecord(const zx::thread& thread,
                                          debug_ipc::ExceptionRecord* out) const {
  out->valid = false;

  zx_exception_report_t report = {};
  zx_status_t status =
      thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr, nullptr);
  if (status != ZX_OK)
    return;

  out->valid = true;
  out->arch.x64.vector = report.context.arch.u.x86_64.vector;
  out->arch.x64.err_code = report.context.arch.u.x86_64.err_code;
  out->arch.x64.cr2 = report.context.arch.u.x86_64.cr2;
}

}  // namespace arch
}  // namespace debug_agent
