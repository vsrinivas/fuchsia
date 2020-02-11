// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/smccc.h>

#include "ddk_priv.h"

zx_status_t arch_smc_call(const zx_smc_parameters_t* params, zx_smc_result_t* result) {
  auto arm_result = arm_smccc_smc(
      params->func_id, params->arg1, params->arg2, params->arg3, params->arg4, params->arg5,
      params->arg6,
      static_cast<uint32_t>(params->secure_os_id) << 16 | static_cast<uint32_t>(params->client_id));

  result->arg0 = arm_result.x0;
  result->arg1 = arm_result.x1;
  result->arg2 = arm_result.x2;
  result->arg3 = arm_result.x3;
  result->arg6 = arm_result.x6;

  return ZX_OK;
}
