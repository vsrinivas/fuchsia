// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <qcom/smc.h>

namespace qcom {

zx_status_t SmcCall(zx_handle_t h, zx_smc_parameters_t* params, zx_smc_result_t* result) {
  zxlogf(DEBUG, "SMC params 0x%X 0x%lX 0x%lX 0x%lX 0x%lX 0x%lX", params->func_id, params->arg1,
         params->arg2, params->arg3, params->arg4, params->arg5);
  auto status = zx_smc_call(h, params, result);
  zxlogf(DEBUG, "SMC results %ld 0x%lX 0x%lX 0x%lX", result->arg0, result->arg1, result->arg2,
         result->arg3);

  constexpr int total_retry_msecs = 2000;
  constexpr int busy_retry_msecs = 30;
  constexpr int busy_retries = total_retry_msecs / busy_retry_msecs;
  int busy_retry = busy_retries;
  while (status == ZX_OK &&  // Wait forever for smc_interrupted, limited for smc_busy replies.
         (result->arg0 == kSmcInterrupted || (result->arg0 == kSmcBusy && busy_retry--))) {
    if (result->arg0 == kSmcBusy) {
      zx_nanosleep(zx_deadline_after(ZX_MSEC(busy_retry_msecs)));
    }
    params->arg6 = result->arg6;  // Pass optional session_id received via x6 back in retry.

    zxlogf(DEBUG, "SMC params 0x%X 0x%lX 0x%lX 0x%lX 0x%lX 0x%lX", params->func_id, params->arg1,
           params->arg2, params->arg3, params->arg4, params->arg5);
    status = zx_smc_call(h, params, result);
    zxlogf(DEBUG, "SMC busy_retry %d results %ld 0x%lX 0x%lX 0x%lX", busy_retries - busy_retry,
           result->arg0, result->arg1, result->arg2, result->arg3);
  }
  if (result->arg0 != 0) {
    zxlogf(ERROR, "%s error %d", __func__, static_cast<int>(result->arg0));
  }
  return status;
}
}  // namespace qcom
