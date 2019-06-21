// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "system_priv.h"

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <trace.h>

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg) {
    return ZX_ERR_NOT_SUPPORTED;
}
