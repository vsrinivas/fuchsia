// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_EL2_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_EL2_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

extern void arm64_el2_gicv3_read_gich_state(zx_paddr_t state);
extern void arm64_el2_gicv3_write_gich_state(zx_paddr_t state, uint32_t hcr);
extern uint32_t arm64_el2_gicv3_read_gich_vtr();

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_EL2_H_
