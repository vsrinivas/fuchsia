// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_SYSCALLS_DDK_PRIV_H_
#define ZIRCON_KERNEL_LIB_SYSCALLS_DDK_PRIV_H_

#include <zircon/syscalls/smc.h>

zx_status_t arch_smc_call(const zx_smc_parameters_t* params, zx_smc_result_t* result);

#endif  // ZIRCON_KERNEL_LIB_SYSCALLS_DDK_PRIV_H_
