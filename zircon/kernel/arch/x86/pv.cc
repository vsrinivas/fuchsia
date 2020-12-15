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
#include <fbl/atomic_ref.h>
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

  // Note: We're setting up one, system-wide PV clock rather than per-CPU system
  // clocks. This is OK because
  //   - the PV clock is only used if it's stable
  //   - we assume invariant TSC if the clock is stable
  //   - we don't read from the clock's tsc_timestamp; we use rdtsc directly
  write_msr(kKvmSystemTimeMsr, pa | kSystemTimeEnable);

  return ZX_OK;
}

void pv_clock_shutdown() {
  DEBUG_ASSERT(arch_curr_cpu_num() == 0);

  // Tell our hypervisor to stop updating the clock.
  write_msr(kKvmSystemTimeMsr, 0);
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
  fbl::atomic_ref<volatile uint32_t> version(system_time->version);
  do {
    pre_version = version.load();
    if (pre_version % 2 != 0) {
      arch::Yield();
      continue;
    }
    tsc_mul = system_time->tsc_mul;
    tsc_shift = system_time->tsc_shift;
    post_version = version.load();
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

static PvEoi g_pv_eoi[SMP_MAX_CPUS];

void PvEoi::InitAll() {
  for (PvEoi& pv : g_pv_eoi) {
    pv.Init();
  }
}

void PvEoi::Init() {
  ZX_DEBUG_ASSERT(!arch_blocking_disallowed());
  ZX_DEBUG_ASSERT(!enabled_.load());
  ZX_DEBUG_ASSERT(state_paddr_ == 0);

  state_paddr_ = vaddr_to_paddr(&state_);
  ZX_DEBUG_ASSERT(state_paddr_ != 0);
  ZX_DEBUG_ASSERT(state_paddr_ % alignof(decltype(PvEoi::state_)) == 0);
}

PvEoi* PvEoi::get() { return &g_pv_eoi[arch_curr_cpu_num()]; }

void PvEoi::Enable(MsrAccess* msr) {
  // It is critical that this method does not block as it may be called early during boot, prior to
  // the calling CPU being marked active.

  ZX_DEBUG_ASSERT(!enabled_.load());
  ZX_DEBUG_ASSERT(state_paddr_ != 0);

  msr->write_msr(X86_MSR_KVM_PV_EOI_EN, state_paddr_ | X86_MSR_KVM_PV_EOI_EN_ENABLE);
  enabled_.store(true, ktl::memory_order_release);
}

void PvEoi::Disable(MsrAccess* msr) {
  // It is critical that this method does not block as it may be called when the current CPU is
  // being shutdown.

  // Mark as disabled before writing to the MSR; otherwise an interrupt appearing in the window
  // between the two could fail to EOI via the legacy mechanism.
  enabled_.store(false, ktl::memory_order_release);
  msr->write_msr(X86_MSR_KVM_PV_EOI_EN, 0);
}

bool PvEoi::Eoi() {
  if (!enabled_.load(ktl::memory_order_relaxed)) {
    return false;
  }

  uint64_t old_val = state_.exchange(0);
  return old_val != 0;
}

PvEoi::~PvEoi() {
  ZX_DEBUG_ASSERT(!enabled_.load());
}
