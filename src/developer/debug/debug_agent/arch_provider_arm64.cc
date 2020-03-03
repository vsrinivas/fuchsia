// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_provider_arm64.h"

#include <zircon/syscalls/exception.h>

namespace debug_agent {
namespace arch {

void ArchProviderArm64::FillExceptionRecord(const zx::thread& thread,
                                            debug_ipc::ExceptionRecord* out) const {
  out->valid = false;

  zx_exception_report_t report = {};
  zx_status_t status =
      thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr, nullptr);
  if (status != ZX_OK)
    return;

  out->valid = true;
  out->arch.arm64.esr = report.context.arch.u.arm_64.esr;
  out->arch.arm64.far = report.context.arch.u.arm_64.far;
}

}  // namespace arch
}  // namespace debug_agent
