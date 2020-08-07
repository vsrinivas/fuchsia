// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>

#include "system_priv.h"

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg) {
  return ZX_ERR_NOT_SUPPORTED;
}
