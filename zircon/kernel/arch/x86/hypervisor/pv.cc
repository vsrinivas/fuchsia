// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <zircon/types.h>

#include <arch/hypervisor.h>
#include <arch/x86/pv.h>
#include <hypervisor/aspace.h>
#include <ktl/atomic.h>
#include <vm/physmap.h>

#include "pv_priv.h"

#include <ktl/enforce.h>

namespace {

void calculate_scale_factor(uint64_t tsc_freq, uint32_t* mul, int8_t* shift) {
  // Guests converts TSC ticks to nanoseconds using this formula:
  //   ns = #TSCticks * mul * 2^(shift - 32).
  // mul * 2^(shift - 32) is a fractional number used as a scale factor in conversion.
  // It's very similar to how floating point numbers are usually represented in memory.
  static const uint64_t target_freq = 1000000000ul;

  DEBUG_ASSERT(tsc_freq != 0);

  // We maintain the folowing invariant: 2^(exponent - 32) * x/y ~ target_freq / tsc_freq,
  int8_t exponent = 32;
  uint64_t x = target_freq;
  uint64_t y = tsc_freq;

  // First make y small enough so that (y << 31) doesn't overflow in the next step. Adjust
  // exponent along the way to maintain invariant.
  while (y >= (1ull << 31)) {
    y >>= 1;
    exponent--;
  }

  // We scale x/y multiplying x by 2 until it gets big enough or we run out of bits.
  while (x < (y << 31) && BIT(x, 63) == 0) {
    x <<= 1;
    exponent--;
  }

  // Though it's very unlikely lets also consider a situation when x/y is still too small.
  while (x < y) {
    y >>= 1;
    exponent++;
  }

  // Finally make sure that x/y fits within 32 bits.
  while (x >= (y << 32)) {
    x >>= 1;
    exponent++;
  }

  *shift = static_cast<int8_t>(exponent);
  *mul = static_cast<uint32_t>(x / y);
}

}  // namespace

zx::result<> pv_clock_update_boot_time(hypervisor::GuestPhysicalAspace* gpa,
                                       zx_vaddr_t guest_paddr) {
  // Zircon does not maintain a UTC or local time to set a meaningful boot time
  // hence the value is fixed at zero.
  auto guest_ptr = gpa->CreateGuestPtr(guest_paddr, sizeof(pv_clock_boot_time),
                                       "pv_clock-boot-time-guest-mapping");
  if (guest_ptr.is_error()) {
    return guest_ptr.take_error();
  }
  auto boot_time = guest_ptr->as<pv_clock_boot_time>();
  if (boot_time == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  memset(boot_time, 0, sizeof(*boot_time));
  return zx::ok();
}

zx::result<> pv_clock_reset_clock(PvClockState* pv_clock, hypervisor::GuestPhysicalAspace* gpa,
                                  zx_vaddr_t guest_paddr) {
  auto guest_ptr = gpa->CreateGuestPtr(guest_paddr, sizeof(pv_clock_system_time),
                                       "pv_clock-system-time-guest-mapping");
  if (guest_ptr.is_error()) {
    return guest_ptr.take_error();
  }
  pv_clock->system_time = guest_ptr->as<pv_clock_system_time>();
  if (pv_clock->system_time == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  memset(pv_clock->system_time, 0, sizeof(*pv_clock->system_time));
  pv_clock->guest_ptr = ktl::move(*guest_ptr);
  return zx::ok();
}

void pv_clock_update_system_time(PvClockState* pv_clock, hypervisor::GuestPhysicalAspace* gpa) {
  if (!pv_clock->system_time) {
    return;
  }

  uint32_t tsc_mul;
  int8_t tsc_shift;
  calculate_scale_factor(ticks_per_second(), &tsc_mul, &tsc_shift);

  // See the comment for pv_clock_boot_time structure in arch/x86/pv.h
  pv_clock_system_time* system_time = pv_clock->system_time;
  ktl::atomic_ref<uint32_t> guest_version(system_time->version);
  guest_version.store(pv_clock->version + 1, ktl::memory_order_relaxed);
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  system_time->tsc_mul = tsc_mul;
  system_time->tsc_shift = tsc_shift;
  system_time->system_time = current_time();
  system_time->tsc_timestamp = _rdtsc();
  system_time->flags = pv_clock->is_stable ? kKvmSystemTimeStable : 0;
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  guest_version.store(pv_clock->version + 2, ktl::memory_order_relaxed);
  pv_clock->version += 2;
}

void pv_clock_stop_clock(PvClockState* pv_clock) {
  pv_clock->system_time = nullptr;
  pv_clock->guest_ptr.reset();
}

zx::result<> pv_clock_populate_offset(hypervisor::GuestPhysicalAspace* gpa,
                                      zx_vaddr_t guest_paddr) {
  auto guest_ptr =
      gpa->CreateGuestPtr(guest_paddr, sizeof(PvClockOffset), "pv_clock-offset-guest-mapping");
  if (guest_ptr.is_error()) {
    return guest_ptr.take_error();
  }
  auto offset = guest_ptr->as<PvClockOffset>();
  if (offset == nullptr) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  memset(offset, 0, sizeof(*offset));
  // Zircon does not maintain a UTC or local time. We populate offset using the
  // only time available - time since the device was powered on.
  zx_time_t time = current_time();
  uint64_t tsc = _rdtsc();
  offset->sec = time / ZX_SEC(1);
  offset->nsec = time % ZX_SEC(1);
  offset->tsc = tsc;
  return zx::ok();
}
