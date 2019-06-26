// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_GICV2_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_GICV2_H_

// clang-format off

#define GICH_LR_PENDING_BIT     28
#define GICH_LR_ACTIVE_BIT      29
#define GICH_LR_VIRTUAL_ID(id)  (id & 0x3ff)
#define GICH_LR_PHYSICAL_ID(id) ((id & 0x3ff) << 10)
#define GICH_LR_PRIORITY(prio)  ((prio & 0x1f) << 23)
#define GICH_LR_GROUP1          (1u << 30)
#define GICH_LR_HARDWARE        (1u << 31)
#define GICH_VMCR_VENG0         (1u << 0)
#define GICH_VMCR_VPMR          (0x1fu << 27)
#define GICH_VTR_PRES(vtr)      (((vtr & (0x7u << 26)) >> 26) + 1)
#define GICH_VTR_LRS(vtr)       ((vtr & 0x3fu) + 1)

// clang-format on

void gicv2_hw_interface_register();

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_HYPERVISOR_GIC_GICV2_H_
