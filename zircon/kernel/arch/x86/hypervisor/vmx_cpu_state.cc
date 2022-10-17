// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <bits.h>
#include <string.h>

#include <arch/x86/hypervisor/invalidate.h>
#include <hypervisor/cpu.h>
#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>

#include "vmx_cpu_state_priv.h"

namespace {

DECLARE_SINGLETON_MUTEX(GuestMutex);
size_t num_guests TA_GUARDED(GuestMutex::Get()) = 0;
fbl::Array<VmxPage> vmxon_pages TA_GUARDED(GuestMutex::Get());

void vmxon(zx_paddr_t pa) {
  uint8_t err;

  __asm__ __volatile__("vmxon %[pa]"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       : [pa] "m"(pa)
                       : "cc", "memory");

  ASSERT(!err);
}

void vmxoff() {
  uint8_t err;

  __asm__ __volatile__("vmxoff"
                       : "=@ccna"(err)  // Set `err` on error (C or Z flag set)
                       :                // no inputs
                       : "cc");

  ASSERT(!err);
}

zx::result<> vmxon_task(void* context, cpu_num_t cpu_num) {
  auto pages = static_cast<fbl::Array<VmxPage>*>(context);
  VmxPage& page = (*pages)[cpu_num];

  // Check that we have instruction information when we VM exit on IO.
  VmxInfo vmx_info;
  if (!vmx_info.io_exit_info) {
    dprintf(CRITICAL, "hypervisor: IO instruction information not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Check that full VMX controls are supported.
  if (!vmx_info.vmx_controls) {
    dprintf(CRITICAL, "hypervisor: VMX controls not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Check that a page-walk length of 4 is supported.
  EptInfo ept_info;
  if (!ept_info.page_walk_4) {
    dprintf(CRITICAL, "hypervisor: VMX controls not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Check that use of the write-back memory type is supported.
  if (!ept_info.write_back) {
    dprintf(CRITICAL, "hypervisor: EPT write-back memory type not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Check that use of large pages is supported.
  if (!ept_info.large_pages) {
    // Warning only.
    dprintf(CRITICAL, "hypervisor: EPT large pages not supported\n");
  }

  // Check that the INVEPT instruction is supported.
  if (!ept_info.invept) {
    dprintf(CRITICAL, "hypervisor: INVEPT instruction not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Check that the INVVPID instruction is supported.
  if (!ept_info.invvpid) {
    dprintf(CRITICAL, "hypervisor: INVVPID instruction not supported\n");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Enable VMXON, if required.
  uint64_t feature_control = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
  if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON) == 0) {
    if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) != 0) {
      dprintf(CRITICAL, "hypervisor: VMX disabled\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    feature_control |= X86_MSR_IA32_FEATURE_CONTROL_LOCK;
    feature_control |= X86_MSR_IA32_FEATURE_CONTROL_VMXON;
    write_msr(X86_MSR_IA32_FEATURE_CONTROL, feature_control);
  }

  // Check control registers are in a VMX-friendly state.
  uint64_t cr0 = x86_get_cr0();
  if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  uint64_t cr4 = x86_get_cr4() | X86_CR4_VMXE;
  if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  // Enable VMX using the VMXE bit.
  x86_set_cr4(cr4);

  // Setup VMXON page.
  VmxRegion* region = page.VirtualAddress<VmxRegion>();
  region->revision_id = vmx_info.revision_id;

  // Execute VMXON.
  vmxon(page.PhysicalAddress());

  // From Volume 3, Section 28.3.3.4: Software can use the INVEPT instruction
  // with the “all-context” INVEPT type immediately after execution of the VMXON
  // instruction or immediately prior to execution of the VMXOFF instruction.
  // Either prevents potentially undesired retention of information cached from
  // EPT paging structures between separate uses of VMX operation.
  invept(InvEpt::GLOBAL, 0);

  return zx::ok();
}

void vmxoff_task(void* arg) {
  // Execute VMXOFF.
  vmxoff();

  // Disable VMX.
  x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
}

}  // namespace

void broadcast_invept(uint64_t eptp) {
  // If there are no guests then do not perform the invept, since vmx will not be on and we will
  // fault. When vmx is turned back on we will perform a global context invalidation anyway, so this
  // is safe. The reason ept invalidations might occur after vmx has been turned off is that the
  // EPT itself can outlive the guests due to user space having their own handles to the EPT aspace.
  Guard<Mutex> guard(GuestMutex::Get());
  if (num_guests != 0) {
    mp_sync_exec(
        MP_IPI_TARGET_ALL, 0,
        [](void* eptp) { invept(InvEpt::SINGLE_CONTEXT, *static_cast<uint64_t*>(eptp)); }, &eptp);
  }
}

VmxInfo::VmxInfo() {
  // From Volume 3, Appendix A.1.
  uint64_t basic_info = read_msr(X86_MSR_IA32_VMX_BASIC);
  revision_id = static_cast<uint32_t>(BITS(basic_info, 30, 0));
  region_size = static_cast<uint16_t>(BITS_SHIFT(basic_info, 44, 32));
  write_back = BITS_SHIFT(basic_info, 53, 50) == VMX_MEMORY_TYPE_WRITE_BACK;
  io_exit_info = BIT_SHIFT(basic_info, 54);
  vmx_controls = BIT_SHIFT(basic_info, 55);
}

EptInfo::EptInfo() {
  // From Volume 3, Appendix A.10.
  uint64_t ept_info = read_msr(X86_MSR_IA32_VMX_EPT_VPID_CAP);
  page_walk_4 = BIT_SHIFT(ept_info, 6);
  write_back = BIT_SHIFT(ept_info, 14);
  large_pages =
      // 2mb pages are supported.
      BIT_SHIFT(ept_info, 16) &&
      // 1gb pages are supported.
      BIT_SHIFT(ept_info, 17);
  invept =
      // INVEPT instruction is supported.
      BIT_SHIFT(ept_info, 20) &&
      // Single-context INVEPT type is supported.
      BIT_SHIFT(ept_info, 25) &&
      // All-context INVEPT type is supported.
      BIT_SHIFT(ept_info, 26);
  invvpid =
      // INVVPID instruction is supported.
      BIT_SHIFT(ept_info, 32) &&
      // Individual-address INVVPID type is supported.
      BIT_SHIFT(ept_info, 40) &&
      // Single-context INVVPID type is supported.
      BIT_SHIFT(ept_info, 41) &&
      // All-context INVVPID type is supported.
      BIT_SHIFT(ept_info, 42) &&
      // Single-context-retaining-globals INVVPID type is supported.
      BIT_SHIFT(ept_info, 43);
}

zx_status_t VmxPage::Alloc(const VmxInfo& vmx_info, uint8_t fill) {
  // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
  // software should allocate for the VMXON region and any VMCS region. It is
  // a value greater than 0 and at most 4096 (bit 44 is set if and only if
  // bits 43:32 are clear).
  if (vmx_info.region_size > PAGE_SIZE) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Check use of write-back memory for VMX regions is supported.
  if (!vmx_info.write_back) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // The maximum size for a VMXON or VMCS region is 4096, therefore
  // unconditionally allocating a page is adequate.
  return hypervisor::Page::Alloc(fill).status_value();
}

zx::result<> alloc_vmx_state() {
  Guard<Mutex> guard(GuestMutex::Get());
  if (num_guests == 0) {
    fbl::AllocChecker ac;
    size_t num_cpus = arch_max_num_cpus();
    VmxPage* pages_ptr = new (&ac) VmxPage[num_cpus];
    if (!ac.check()) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }
    fbl::Array<VmxPage> pages(pages_ptr, num_cpus);
    VmxInfo vmx_info;
    for (auto& page : pages) {
      if (zx_status_t status = page.Alloc(vmx_info, 0); status != ZX_OK) {
        return zx::error(status);
      }
    }

    // Enable VMX for all online CPUs.
    cpu_mask_t cpu_mask = percpu_exec(vmxon_task, &pages);
    if (cpu_mask != mp_get_online_mask()) {
      mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask, vmxoff_task, nullptr);
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }

    vmxon_pages = ktl::move(pages);
  }
  num_guests++;
  return zx::ok();
}

void free_vmx_state() {
  Guard<Mutex> guard(GuestMutex::Get());
  num_guests--;
  if (num_guests == 0) {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, vmxoff_task, nullptr);
    vmxon_pages.reset();
  }
}

bool cr_is_invalid(uint64_t cr_value, uint32_t fixed0_msr, uint32_t fixed1_msr) {
  uint64_t fixed0 = read_msr(fixed0_msr);
  uint64_t fixed1 = read_msr(fixed1_msr);
  return ~(cr_value | ~fixed0) != 0 || ~(~cr_value | fixed1) != 0;
}
