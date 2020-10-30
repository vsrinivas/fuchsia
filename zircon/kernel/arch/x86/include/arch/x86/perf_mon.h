// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file contains declarations internal to x86.
// Declarations visible outside of x86 belong in arch_perfmon.h.

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PERF_MON_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PERF_MON_H_

#include <arch/x86.h>

void apic_pmi_interrupt_handler(iframe_t *frame);

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PERF_MON_H_
