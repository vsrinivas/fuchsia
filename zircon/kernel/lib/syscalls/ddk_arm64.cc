// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/lockup_detector.h>
#include <lib/zircon-internal/macros.h>

#include <arch/arm64/smccc.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/event_limiter.h>
#include <kernel/scheduler.h>

#include "ddk_priv.h"

namespace {

// Rate limit the OOPS message to avoid spamming the log.
EventLimiter<ZX_SEC(1)> oops_rate_limiter;

// Emit an OOPS if the current thread has exceeded its targeted preemption time by |threshold|.
//
// Return true if |threshold| was exceeded.
//
// Must be called with preemption disabled.
bool CheckForOverrun(zx_duration_t threshold) {
  DEBUG_ASSERT(Thread::Current::preemption_state().PreemptDisableCount() > 0);

  const zx_time_t now = current_time();
  const zx_time_t target_preemption_time = Scheduler::GetTargetPreemptionTime();
  const zx_duration_t overrun = zx_time_sub_time(now, target_preemption_time);
  if (overrun > threshold && oops_rate_limiter.Ready()) {
    printf(
        "WARNING: lockup_detector: thread has overrun its preemption time, overrun=%ldns, "
        "threshold=%ldns (message rate limited)\n",
        overrun, threshold);
    return true;
  }

  return false;
}

}  // namespace

zx_status_t arch_smc_call(const zx_smc_parameters_t* params, zx_smc_result_t* result) {
  const uint32_t client_and_secure_os_id =
      static_cast<uint32_t>(params->secure_os_id) << 16 | static_cast<uint32_t>(params->client_id);
  arm_smccc_result_t arm_result;
  {
    AutoPreemptDisabler disabler;

    const zx_time_t before = current_time();
    LOCKUP_TIMED_BEGIN(SOURCE_TAG);
    arm_result = arm_smccc_smc(params->func_id, params->arg1, params->arg2, params->arg3,
                               params->arg4, params->arg5, params->arg6, client_and_secure_os_id);
    LOCKUP_TIMED_END();
    const zx_duration_t delta = zx_time_sub_time(current_time(), before);

    // Amount of time this thread may overrun its target preemption time before an OOPS is emitted.
    //
    // This value should be larger than the longest running SMC Fast Call, but small enough to
    // detect temporary hangs and issues that could affect system performance or interactivity.
    constexpr zx_duration_t kOverrunThreshold = ZX_MSEC(10);

    // Were we in EL3 longer than we should have been?
    if (CheckForOverrun(kOverrunThreshold)) {
      printf("SMC arguments: w0=0x%" PRIx32 ", x1=0x%" PRIx64 ", x2=0x%" PRIx64 ", x3=0x%" PRIx64
             ", x4=0x%" PRIx64 ", x5=0x%" PRIx64 ", x6=0x%" PRIx64 ", w7=0x%" PRIx32
             "\nSMC results:   x0=0x%" PRIx64 ", x1=0x%" PRIx64 ", x2=0x%" PRIx64 ", x3=0x%" PRIx64
             ", x6=0x%" PRIx64 "\nduration=%ldns\n",
             params->func_id, params->arg1, params->arg2, params->arg3, params->arg4, params->arg5,
             params->arg6, client_and_secure_os_id, arm_result.x0, arm_result.x1, arm_result.x2,
             arm_result.x3, arm_result.x6, delta);
    }
  }

  result->arg0 = arm_result.x0;
  result->arg1 = arm_result.x1;
  result->arg2 = arm_result.x2;
  result->arg3 = arm_result.x3;
  result->arg6 = arm_result.x6;

  return ZX_OK;
}
