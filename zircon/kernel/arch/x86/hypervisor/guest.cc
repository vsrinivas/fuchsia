// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor/invalidate.h>

#include "vmx_cpu_state_priv.h"

namespace {

constexpr size_t kGuestKernelBaseOrSize = PAGE_ALIGN(USER_ASPACE_SIZE / 2);

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
template <typename G>
zx::status<ktl::unique_ptr<G>> Guest::Create() {
  // Check that the CPU supports VMX.
  if (!x86_feature_test(X86_FEATURE_VMX)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (auto result = alloc_vmx_state(); result.is_error()) {
    return result.take_error();
  }
  auto defer = fit::defer([] { free_vmx_state(); });

  fbl::AllocChecker ac;
  auto guest = ktl::make_unique<G>(&ac);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  defer.cancel();

  // Setup common MSR bitmaps.
  VmxInfo vmx_info;
  if (zx_status_t status = guest->msr_bitmaps_page_.Alloc(vmx_info, UINT8_MAX); status != ZX_OK) {
    return zx::error(status);
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

  return zx::ok(ktl::move(guest));
}

Guest::~Guest() { free_vmx_state(); }

// static
zx::status<ktl::unique_ptr<Guest>> NormalGuest::Create() {
  auto gpa = hypervisor::GuestPhysicalAspace::Create();
  if (gpa.is_error()) {
    return gpa.take_error();
  }
  // Invalidate the EPT across all CPUs.
  uint64_t eptp = ept_pointer_from_pml4(gpa->arch_aspace().arch_table_phys());
  broadcast_invept(eptp);

  auto guest = Guest::Create<NormalGuest>();
  if (guest.is_error()) {
    return guest.take_error();
  }
  guest->gpa_ = ktl::move(*gpa);
  return ktl::move(guest);
}

zx_status_t NormalGuest::SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
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
      return traps_.InsertTrap(kind, addr, len, nullptr, key).status_value();
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  // Common logic for memory-based traps.
  if (!IS_PAGE_ALIGNED(addr) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (auto result = gpa_.UnmapRange(addr, len); result.is_error()) {
    return result.status_value();
  }
  return traps_.InsertTrap(kind, addr, len, ktl::move(port), key).status_value();
}

// static
zx::status<ktl::unique_ptr<Guest>> DirectGuest::Create() {
  auto dpa = hypervisor::DirectPhysicalAspace::Create();
  if (dpa.is_error()) {
    return dpa.take_error();
  }
  // Invalidate the EPT across all CPUs.
  uint64_t eptp = ept_pointer_from_pml4(dpa->arch_aspace().arch_table_phys());
  broadcast_invept(eptp);

  auto user_aspace = VmAspace::Create(kGuestKernelBaseOrSize, kGuestKernelBaseOrSize,
                                      VmAspace::Type::User, "guest_kernel");
  if (!user_aspace) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  user_aspace->arch_aspace().arch_set_vpid(kGlobalAspaceVpid);

  auto guest = Guest::Create<DirectGuest>();
  if (guest.is_error()) {
    return guest.take_error();
  }
  guest->dpa_ = ktl::move(*dpa);
  guest->user_aspace_ = ktl::move(user_aspace);
  return ktl::move(guest);
}

DirectGuest::~DirectGuest() {
  // Reset the VPID associated with the address space, so that if VMX is turned
  // off, we do not issue an `invvpid`.
  //
  // This is safe, as we always `invept` all CPUs when creating a guest, and
  // then `invvpid` on the current CPU when creating a VCPU.
  user_aspace_->arch_aspace().arch_set_vpid(MMU_X86_UNUSED_VPID);
}
