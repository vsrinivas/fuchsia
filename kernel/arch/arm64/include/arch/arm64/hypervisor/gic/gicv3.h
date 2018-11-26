// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// clang-format off

#define ICH_LR_VIRTUAL_ID(id)  (id & UINT32_MAX)
#define ICH_LR_PHYSICAL_ID(id) ((id & 0x3fful) << 32)
#define ICH_LR_PRIORITY(prio)  ((prio & 0xfful) << 48)
#define ICH_LR_GROUP1          (1ul << 60)
#define ICH_LR_HARDWARE        (1ul << 61)
#define ICH_LR_PENDING         (1ul << 62)
#define ICH_VMCR_VENG1         (1u << 1)
#define ICH_VMCR_VPMR_MASK     (0xffu << 24)
#define ICH_VTR_LIST_REGS_MASK 0x1f

// clang-format on

void gicv3_hw_interface_register();
