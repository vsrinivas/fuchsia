// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

#define ICH_LR_VIRTUAL_ID_MASK UINT32_MAX

__BEGIN_CDECLS;

void gicv3_hw_interface_register(void);

__END_CDECLS;
