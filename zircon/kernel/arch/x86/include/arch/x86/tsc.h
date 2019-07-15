// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_TSC_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_TSC_H_

void x86_tsc_adjust(void);
void x86_tsc_store_adjustment(void);

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_TSC_H_
