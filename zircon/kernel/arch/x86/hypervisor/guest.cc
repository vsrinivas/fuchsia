// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>

#include "vmx_cpu_state_priv.h"

namespace {

void IgnoreMsr(const VmxPage& msr_bitmaps_page, uint32_t msr) {
  // From Volume 3, Section 24.6.9.
  uint8_t* msr_bitmaps = msr_bitmaps_page.VirtualAddress<uint8_t>();
  if (msr >= 0xc0000000) {
    msr_bitmaps += 1 << 10;
  }

  uint16_t msr_low = msr & 0x1fff;
  uint16_t msr_byte = msr_low / 8;
  uint8_t msr_bit = msr_low % 8;

  // Ignore reads to the MSR.
  msr_bitmaps[msr_byte] &= static_cast<uint8_t>(~(1u << msr_bit));

  // Ignore writes to the MSR.
  msr_bitmaps += 2 << 10;
  msr_bitmaps[msr_byte] &= static_cast<uint8_t>(~(1u << msr_bit));
}

}  // namespace

// static
zx_status_t Guest::Create(ktl::unique_ptr<Guest>* out) {
  // Check that the CPU supports VMX.
  if (!x86_feature_test(X86_FEATURE_VMX)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (auto result = alloc_vmx_state(); result.is_error()) {
    return result.status_value();
  }
  auto defer = fit::defer([] { free_vmx_state(); });

  fbl::AllocChecker ac;
  ktl::unique_ptr<Guest> guest(new (&ac) Guest);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  defer.cancel();

  auto gpas = hypervisor::GuestPhysicalAddressSpace::Create();
  if (gpas.is_error()) {
    return gpas.status_value();
  }
  guest->gpas_ = ktl::move(*gpas);

  // Setup common MSR bitmaps.
  VmxInfo vmx_info;
  if (zx_status_t status = guest->msr_bitmaps_page_.Alloc(vmx_info, UINT8_MAX); status != ZX_OK) {
    return status;
  }

  // These are saved/restored by VMCS controls.
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_SYSENTER_CS);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_SYSENTER_ESP);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_SYSENTER_EIP);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_PAT);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_EFER);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_FS_BASE);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_GS_BASE);

  // These are handled by MSR-load / MSR-store areas.
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_STAR);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_LSTAR);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_FMASK);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_KERNEL_GS_BASE);
  IgnoreMsr(guest->msr_bitmaps_page_, X86_MSR_IA32_TSC_AUX);

  *out = ktl::move(guest);
  return ZX_OK;
}

Guest::~Guest() { free_vmx_state(); }

zx_status_t Guest::SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  switch (kind) {
    case ZX_GUEST_TRAP_MEM:
      if (port) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    case ZX_GUEST_TRAP_BELL:
      if (!port) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    case ZX_GUEST_TRAP_IO:
      if (port) {
        return ZX_ERR_INVALID_ARGS;
      }
      return traps_.InsertTrap(kind, addr, len, ktl::move(port), key).status_value();
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // Common logic for memory-based traps.
  if (!IS_PAGE_ALIGNED(addr) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (auto result = gpas_.UnmapRange(addr, len); result.is_error()) {
    return result.status_value();
  }
  return traps_.InsertTrap(kind, addr, len, ktl::move(port), key).status_value();
}
