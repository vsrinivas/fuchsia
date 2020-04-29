// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/pv.h"

#include <lib/arch/intrin.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <arch/x86/registers.h>
#include <kernel/atomic.h>
#include <ktl/atomic.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

// Paravirtual functions, to execute some functions in a Hypervisor-specific way.
// The paravirtual optimizations in this file are implemented by kvm/qemu.

static volatile pv_clock_boot_time* boot_time = nullptr;
static volatile pv_clock_system_time* system_time = nullptr;

static constexpr uint64_t kSystemTimeEnable = 1u;

zx_status_t pv_clock_init(void) {
  if (boot_time != nullptr || system_time != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  paddr_t pa;
  zx_status_t status = pmm_alloc_page(0, &pa);
  if (status != ZX_OK) {
    return status;
  }
  arch_zero_page(paddr_to_physmap(pa));
  boot_time = static_cast<pv_clock_boot_time*>(paddr_to_physmap(pa));
  write_msr(kKvmBootTime, pa);

  status = pmm_alloc_page(0, &pa);
  if (status != ZX_OK) {
    return status;
  }
  arch_zero_page(paddr_to_physmap(pa));
  system_time = static_cast<pv_clock_system_time*>(paddr_to_physmap(pa));
  write_msr(kKvmSystemTimeMsr, pa | kSystemTimeEnable);

  return ZX_OK;
}

bool pv_clock_is_stable() {
  bool is_stable = (system_time->flags & kKvmSystemTimeStable) ||
                   x86_feature_test(X86_FEATURE_KVM_PV_CLOCK_STABLE);
  printf("pv_clock: Clocksource is %sstable\n", (is_stable ? "" : "not "));
  return is_stable;
}

uint64_t pv_clock_get_tsc_freq() {
  printf("pv_clock: Fetching TSC frequency\n");
  uint32_t tsc_mul = 0;
  int8_t tsc_shift = 0;
  uint32_t pre_version = 0, post_version = 0;
  do {
    pre_version = atomic_load_u32(&system_time->version);
    if (pre_version % 2 != 0) {
      arch::Yield();
      continue;
    }
    tsc_mul = system_time->tsc_mul;
    tsc_shift = system_time->tsc_shift;
    post_version = atomic_load_u32(&system_time->version);
  } while (pre_version != post_version);

  uint64_t tsc_khz = 1000000ULL << 32;
  tsc_khz = tsc_khz / tsc_mul;
  if (tsc_shift > 0) {
    tsc_khz >>= tsc_shift;
  } else {
    tsc_khz <<= -tsc_shift;
  }
  return tsc_khz * 1000;
}

int pv_ipi(uint64_t mask_low, uint64_t mask_high, uint64_t start_id, uint64_t icr) {
  static constexpr uint32_t kPvIpiNum = 10;

  int ret;
  switch (x86_vendor) {
    case X86_VENDOR_INTEL:
      __asm__ __volatile__("vmcall"
                           : "=a"(ret)
                           : "a"(kPvIpiNum), "b"(mask_low), "c"(mask_high), "d"(start_id), "S"(icr)
                           : "memory");
      break;
    case X86_VENDOR_AMD:
      __asm__ __volatile__("vmmcall"
                           : "=a"(ret)
                           : "a"(kPvIpiNum), "b"(mask_low), "c"(mask_high), "d"(start_id), "S"(icr)
                           : "memory");
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }
  return ret;
}

namespace pv {

static PvEoi g_pv_eoi[SMP_MAX_CPUS];

PvEoi* PvEoi::get() { return &g_pv_eoi[arch_curr_cpu_num()]; }

void PvEoi::Enable(MsrAccess* msr) {
  paddr_t state_page_paddr;
  zx_status_t status = pmm_alloc_page(0, &state_page_, &state_page_paddr);
  ZX_ASSERT(status == ZX_OK);

  arch_zero_page(paddr_to_physmap(state_page_paddr));
  state_ = static_cast<uint64_t*>(paddr_to_physmap(state_page_paddr));
  msr->write_msr(X86_MSR_KVM_PV_EOI_EN, state_page_paddr | X86_MSR_KVM_PV_EOI_EN_ENABLE);

  enabled_.store(true, ktl::memory_order_release);
}

void PvEoi::Disable(MsrAccess* msr) {
  // Mark as disabled before writing to the MSR; otherwise an interrupt appearing in the window
  // between the two could fail to EOI via the legacy mechanism.
  enabled_.store(false, ktl::memory_order_release);
  msr->write_msr(X86_MSR_KVM_PV_EOI_EN, 0);
  pmm_free_page(state_page_);
}

bool PvEoi::Eoi() {
  if (!enabled_.load(ktl::memory_order_relaxed)) {
    return false;
  }

  uint64_t old_val = atomic_swap_u64(state_, 0u);
  return old_val != 0;
}

}  // namespace pv
