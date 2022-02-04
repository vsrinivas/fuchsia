// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_AMD_H_
#define ZIRCON_KERNEL_ARCH_X86_AMD_H_

#include <arch/x86/cpuid.h>
#include <arch/x86/platform_access.h>

extern void x86_amd_set_lfence_serializing(const cpu_id::CpuId*, MsrAccess*);

#endif  // ZIRCON_KERNEL_ARCH_X86_AMD_H_
