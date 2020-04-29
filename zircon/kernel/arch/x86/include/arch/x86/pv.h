// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PV_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PV_H_

#include <zircon/types.h>

#include <ktl/atomic.h>
#include <vm/pmm.h>

static constexpr uint32_t kKvmSystemTimeMsrOld = 0x12;
static constexpr uint32_t kKvmSystemTimeMsr = 0x4b564d01;

static constexpr uint32_t kKvmBootTimeOld = 0x11;
static constexpr uint32_t kKvmBootTime = 0x4b564d00;

static constexpr uint32_t kKvmFeatureClockSourceOld = 1u << 0;
static constexpr uint32_t kKvmFeatureClockSource = 1u << 3;

static constexpr uint8_t kKvmSystemTimeStable = 1u << 0;

// Both structures below are part of the ABI used by Xen and KVM, this ABI is not
// defined by use we just follow it. For more detail please refer to the
// documentation (https://www.kernel.org/doc/Documentation/virtual/kvm/msr.txt).
struct pv_clock_boot_time {
  // With multiple VCPUs it is possible that one VCPU can try to read boot time
  // while we are updating it because another VCPU asked for the update. In this
  // case odd version value serves as an indicator for the guest that update is
  // in progress. Therefore we need to update version before we write anything
  // else and after, also we need to user proper memory barriers. The same logic
  // applies to system time version below, even though system time is per VCPU
  // others VCPUs still can access system times of other VCPUs (Linux however
  // never does that).
  uint32_t version;
  uint32_t seconds;
  uint32_t nseconds;
};
static_assert(sizeof(struct pv_clock_boot_time) == 12, "sizeof(pv_clock_boot_time) should be 12");

struct pv_clock_system_time {
  uint32_t version;
  uint32_t pad0;
  uint64_t tsc_timestamp;
  uint64_t system_time;
  uint32_t tsc_mul;
  int8_t tsc_shift;
  uint8_t flags;
  uint8_t pad1[2];
};
static_assert(sizeof(struct pv_clock_system_time) == 32,
              "sizeof(pv_clock_system_time) should be 32");

zx_status_t pv_clock_init();
bool pv_clock_is_stable();
uint64_t pv_clock_get_tsc_freq();

// Send para-virtualized IPI.
//
// @param mask_low Low part of CPU mask.
// @param mask_high High part of CPU mask.
// @param start_id APIC ID that the CPU mask starts at.
// @param icr APIC ICR value.
// @return The number of CPUs that the IPI was delivered to, or an error value.
int pv_ipi(uint64_t mask_low, uint64_t mask_high, uint64_t start_id, uint64_t icr);

class MsrAccess;

namespace pv {

class PvEoi final {
 public:
  // Get the current CPU's PV_EOI state.
  static PvEoi* get();

  // Enable PV_EOI for the current CPU. After it is enabled, callers may use Eoi() rather than
  // access a local APIC register if desired.
  void Enable(MsrAccess* msr);

  // Disable PV_EOI for the current CPU.
  void Disable(MsrAccess* msr);

  // Attempt to acknowledge and signal an end-of-interrupt (EOI) for the current CPU via a
  // paravirtual interface. If a fast acknowledge was not available, the function returns
  // false and the caller must signal an EOI via the legacy mechanism.
  bool Eoi();

 private:
  vm_page_t* state_page_;
  uint64_t* state_;

  ktl::atomic<bool> enabled_;
};

}  // namespace pv

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_PV_H_
