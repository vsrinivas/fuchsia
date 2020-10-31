// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <lib/fixed_point.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

#include <arch/riscv64/sbi.h>
#include <dev/interrupt.h>
#include <ktl/atomic.h>
#include <ktl/limits.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <platform/timer.h>

#define LOCAL_TRACE 0

void riscv64_timer_exception(void) {
  riscv64_csr_clear(RISCV64_CSR_SIE, RISCV64_CSR_SIE_TIE);
  timer_tick(current_time());
}

zx_ticks_t platform_current_ticks() {
  return riscv64_get_time();
}

zx_status_t platform_set_oneshot_timer(zx_time_t deadline) {
  DEBUG_ASSERT(arch_ints_disabled());

  if (deadline < 0) {
    deadline = 0;
  }

  // enable the timer
  riscv64_csr_set(RISCV64_CSR_SIE, RISCV64_CSR_SIE_TIE);

  // convert interval to ticks
  const affine::Ratio time_to_ticks = platform_get_ticks_to_time_ratio().Inverse();
  const uint64_t ticks = time_to_ticks.Scale(deadline) + 1;
  sbi_set_timer(ticks);

  return ZX_OK;
}

void platform_stop_timer(void) {
  riscv64_csr_clear(RISCV64_CSR_SIE, RISCV64_CSR_SIE_TIE);
}

void platform_shutdown_timer(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  riscv64_csr_clear(RISCV64_CSR_SIE, RISCV64_CSR_SIE_TIE);
}

bool platform_usermode_can_access_tick_registers(void) {
  return false;
}

template <bool AllowDebugPrint = false>
static inline affine::Ratio riscv_generic_timer_compute_conversion_factors(uint32_t cntfrq) {
  affine::Ratio cntpct_to_nsec = {ZX_SEC(1), cntfrq};
  if constexpr (AllowDebugPrint) {
    dprintf(SPEW, "riscv generic timer cntpct_per_nsec: %u/%u\n", cntpct_to_nsec.numerator(),
            cntpct_to_nsec.denominator());
  }
  return cntpct_to_nsec;
}

static void riscv_generic_timer_pdev_init(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_riscv_generic_timer_driver_t));
  auto driver = static_cast<const dcfg_riscv_generic_timer_driver_t*>(driver_data);
  platform_set_ticks_to_time_ratio(riscv_generic_timer_compute_conversion_factors<true>(driver->freq_hz));
}

LK_PDEV_INIT(riscv_generic_timer_pdev_init, KDRV_RISCV_GENERIC_TIMER, riscv_generic_timer_pdev_init,
             LK_INIT_LEVEL_PLATFORM_EARLY)

