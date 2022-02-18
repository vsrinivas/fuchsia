// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_DIAGNOSTICS_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_DIAGNOSTICS_H_

#include <inttypes.h>

#include <arch/defines.h>
#include <arch/vm.h>

#if defined(__aarch64__)
#include <arch/arm64/dap.h>
#include <arch/arm64/mmu.h>
#endif

namespace lockup_internal {

void DumpRegistersAndBacktrace(cpu_num_t cpu, FILE* output_target);

enum class FailureSeverity { Oops, Fatal };
void DumpCommonDiagnostics(cpu_num_t cpu, FILE* output_target, FailureSeverity severity);

}  // namespace lockup_internal

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_DIAGNOSTICS_H_
