// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// clang-format off

#define GICH_HCR_EN                 (1u << 0)
#define GICH_LR_PENDING             (0b01 << 28)
#define GICH_LR_VIRTUAL_ID_MASK     0x3ff
#define GICH_VTR_LIST_REGS_MASK     0x3f

// clang-format on

__BEGIN_CDECLS;

void gicv2_hw_interface_register(void);

__END_CDECLS;
