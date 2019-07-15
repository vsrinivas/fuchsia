// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file contains declarations internal to arm64.
// Declarations visible outside of arm64 belong in arch_perfmon.h.

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_PERF_MON_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_PERF_MON_H_

#include <arch/arm64.h>

void arm64_pmi_interrupt_handler(const iframe_short_t* frame);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_PERF_MON_H_
