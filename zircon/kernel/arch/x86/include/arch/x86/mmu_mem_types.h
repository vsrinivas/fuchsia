// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_MMU_MEM_TYPES_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_MMU_MEM_TYPES_H_

#include <zircon/compiler.h>

#include <kernel/cpu.h>

__BEGIN_CDECLS

void x86_mmu_mem_type_init(void);
void x86_pat_sync(cpu_mask_t targets);

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_MMU_MEM_TYPES_H_
