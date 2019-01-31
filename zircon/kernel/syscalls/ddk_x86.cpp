// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "ddk_priv.h"

zx_status_t arch_smc_call(const zx_smc_parameters_t* params, zx_smc_result_t* result) {
    return ZX_ERR_NOT_SUPPORTED;
}
