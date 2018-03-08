// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

#define ICH_LR_VIRTUAL_ID_MASK     UINT32_MAX
#define ICH_LR_PENDING             (1UL << 62)
#define ICH_VTR_LIST_REGS_MASK     0x1f
#define ICH_LR_GROUP1              (1UL << 60)

__BEGIN_CDECLS;

void gicv3_hw_interface_register(void);
bool gicv3_is_gic_registered(void);

__END_CDECLS;
