// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HWP_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_HWP_H_

#include <arch/x86/platform_access.h>

void x86_intel_hwp_init(MsrAccess*);

#endif
