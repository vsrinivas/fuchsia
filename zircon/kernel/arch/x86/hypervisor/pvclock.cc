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
#include <arch/x86/pvclock.h>
#include <hypervisor/guest_physical_address_space.h>
#include <ktl/atomic.h>
#include <vm/physmap.h>

#include "pvclock_priv.h"

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

extern ktl::atomic<int64_t> utc_offset;
namespace {
DECLARE_SINGLETON_MUTEX(UpdateBootTimeLock);
}  // namespace

zx_status_t pvclock_update_boot_time(hypervisor::GuestPhysicalAddressSpace* gpas,
                                     zx_vaddr_t guest_paddr) {
  // KVM doesn't provide any protection against concurrent wall time requests from different
  // VCPUs, but documentation doesn't mention that it cannot happen and moreover it properly
  // protects per VCPU system time. Therefore to be on the safer side we use one global mutex
  // for protection.
  static uint32_t version TA_GUARDED(UpdateBootTimeLock::Get());

  hypervisor::GuestPtr guest_ptr;
  zx_status_t status = gpas->CreateGuestPtr(guest_paddr, sizeof(pvclock_boot_time),
                                            "pvclock-boot-time-guest-mapping", &guest_ptr);
  if (status != ZX_OK) {
    return status;
  }
  auto boot_time = guest_ptr.as<pvclock_boot_time>();
  ZX_DEBUG_ASSERT(boot_time != nullptr);
  memset(boot_time, 0, sizeof(*boot_time));

  Guard<Mutex> guard(UpdateBootTimeLock::Get());
  zx_time_t time = utc_offset.load();
  // See the comment for pvclock_boot_time structure in arch/x86/pvclock.h
  atomic_store_relaxed_u32(&boot_time->version, version + 1);
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  boot_time->seconds = static_cast<uint32_t>(time / ZX_SEC(1));
  boot_time->nseconds = static_cast<uint32_t>(time % ZX_SEC(1));
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  atomic_store_relaxed_u32(&boot_time->version, version + 2);
  version += 2;
  return ZX_OK;
}

zx_status_t pvclock_reset_clock(PvClockState* pvclock, hypervisor::GuestPhysicalAddressSpace* gpas,
                                zx_vaddr_t guest_paddr) {
  zx_status_t status =
      gpas->CreateGuestPtr(guest_paddr, sizeof(pvclock_system_time),
                           "pvclock-system-time-guest-mapping", &pvclock->guest_ptr);
  if (status != ZX_OK) {
    return status;
  }
  pvclock->system_time = pvclock->guest_ptr.as<pvclock_system_time>();
  ZX_DEBUG_ASSERT(pvclock->system_time != nullptr);
  memset(pvclock->system_time, 0, sizeof(*pvclock->system_time));
  return ZX_OK;
}

void pvclock_update_system_time(PvClockState* pvclock,
                                hypervisor::GuestPhysicalAddressSpace* gpas) {
  if (!pvclock->system_time) {
    return;
  }

  uint32_t tsc_mul;
  int8_t tsc_shift;
  calculate_scale_factor(ticks_per_second(), &tsc_mul, &tsc_shift);

  // See the comment for pvclock_boot_time structure in arch/x86/pvclock.h
  pvclock_system_time* system_time = pvclock->system_time;
  atomic_store_relaxed_u32(&system_time->version, pvclock->version + 1);
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  system_time->tsc_mul = tsc_mul;
  system_time->tsc_shift = tsc_shift;
  system_time->system_time = current_time();
  system_time->tsc_timestamp = rdtsc();
  system_time->flags = pvclock->is_stable ? kKvmSystemTimeStable : 0;
  ktl::atomic_thread_fence(ktl::memory_order_seq_cst);
  atomic_store_relaxed_u32(&system_time->version, pvclock->version + 2);
  pvclock->version += 2;
}

void pvclock_stop_clock(PvClockState* pvclock) {
  pvclock->system_time = nullptr;
  pvclock->guest_ptr.reset();
}

zx_status_t pvclock_populate_offset(hypervisor::GuestPhysicalAddressSpace* gpas,
                                    zx_vaddr_t guest_paddr) {
  hypervisor::GuestPtr guest_ptr;
  zx_status_t status = gpas->CreateGuestPtr(guest_paddr, sizeof(PvClockOffset),
                                            "pvclock-offset-guest-mapping", &guest_ptr);
  if (status != ZX_OK) {
    return status;
  }
  auto offset = guest_ptr.as<PvClockOffset>();
  ZX_DEBUG_ASSERT(offset != nullptr);
  memset(offset, 0, sizeof(*offset));
  zx_time_t time = utc_offset.load() + current_time();
  uint64_t tsc = rdtsc();
  offset->sec = time / ZX_SEC(1);
  offset->nsec = time % ZX_SEC(1);
  offset->tsc = tsc;
  return ZX_OK;
}
