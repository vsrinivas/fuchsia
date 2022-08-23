// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/suspend.h"

#include <arch/ops.h>

zx_status_t set_suspend_registers(uint8_t sleep_state, uint8_t sleep_type_a, uint8_t sleep_type_b) {
  ASSERT(arch_ints_disabled());
  return ZX_ERR_NOT_SUPPORTED;
}
