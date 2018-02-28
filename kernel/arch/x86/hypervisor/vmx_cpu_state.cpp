// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmx_cpu_state_priv.h"

#include <assert.h>
#include <bits.h>
#include <string.h>

#include <hypervisor/cpu.h>
#include <kernel/auto_lock.h>
#include <kernel/mp.h>

#include <fbl/mutex.h>

static fbl::Mutex guest_mutex;
static size_t num_guests TA_GUARDED(guest_mutex) = 0;
static fbl::Array<VmxPage> vmxon_pages TA_GUARDED(guest_mutex);

static zx_status_t vmxon(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmxon %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? ZX_ERR_INTERNAL : ZX_OK;
}

static zx_status_t vmxoff() {
    uint8_t err;

    __asm__ volatile(
        "vmxoff;" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        :
        : "cc");

    return err ? ZX_ERR_INTERNAL : ZX_OK;
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
    invept =
        // INVEPT instruction is supported.
        BIT_SHIFT(ept_info, 20) &&
        // Single-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 25) &&
        // All-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 26);
}

zx_status_t VmxPage::Alloc(const VmxInfo& vmx_info, uint8_t fill) {
    // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
    // software should allocate for the VMXON region and any VMCS region. It is
    // a value greater than 0 and at most 4096 (bit 44 is set if and only if
    // bits 43:32 are clear).
    if (vmx_info.region_size > PAGE_SIZE)
        return ZX_ERR_NOT_SUPPORTED;

    // Check use of write-back memory for VMX regions is supported.
    if (!vmx_info.write_back)
        return ZX_ERR_NOT_SUPPORTED;

    // The maximum size for a VMXON or VMCS region is 4096, therefore
    // unconditionally allocating a page is adequate.
    return hypervisor::Page::Alloc(fill);
}

static zx_status_t vmxon_task(void* context, cpu_num_t cpu_num) {
    auto pages = static_cast<fbl::Array<VmxPage>*>(context);
    VmxPage& page = (*pages)[cpu_num];

    // Check that we have instruction information when we VM exit on IO.
    VmxInfo vmx_info;
    if (!vmx_info.io_exit_info)
        return ZX_ERR_NOT_SUPPORTED;

    // Check that full VMX controls are supported.
    if (!vmx_info.vmx_controls)
        return ZX_ERR_NOT_SUPPORTED;

    // Check that a page-walk length of 4 is supported.
    EptInfo ept_info;
    if (!ept_info.page_walk_4)
        return ZX_ERR_NOT_SUPPORTED;

    // Check use write-back memory for EPT is supported.
    if (!ept_info.write_back)
        return ZX_ERR_NOT_SUPPORTED;

    // Check that the INVEPT instruction is supported.
    if (!ept_info.invept)
        return ZX_ERR_NOT_SUPPORTED;

    // Enable VMXON, if required.
    uint64_t feature_control = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
    if (!(feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) ||
        !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
        if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) &&
            !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_LOCK;
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_VMXON;
        write_msr(X86_MSR_IA32_FEATURE_CONTROL, feature_control);
    }

    // Check control registers are in a VMX-friendly state.
    uint64_t cr0 = x86_get_cr0();
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1))
        return ZX_ERR_BAD_STATE;
    uint64_t cr4 = x86_get_cr4() | X86_CR4_VMXE;
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1))
        return ZX_ERR_BAD_STATE;

    // Enable VMX using the VMXE bit.
    x86_set_cr4(cr4);

    // Setup VMXON page.
    VmxRegion* region = page.VirtualAddress<VmxRegion>();
    region->revision_id = vmx_info.revision_id;

    // Execute VMXON.
    zx_status_t status = vmxon(page.PhysicalAddress());
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Failed to turn on VMX on CPU %u\n", cpu_num);
        return status;
    }

    return ZX_OK;
}

static void vmxoff_task(void* arg) {
    // Execute VMXOFF.
    zx_status_t status = vmxoff();
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Failed to turn off VMX on CPU %u\n", arch_curr_cpu_num());
        return;
    }

    // Disable VMX.
    x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
}

zx_status_t alloc_vmx_state() {
    fbl::AutoLock lock(&guest_mutex);
    if (num_guests == 0) {
        fbl::AllocChecker ac;
        size_t num_cpus = arch_max_num_cpus();
        VmxPage* pages_ptr = new (&ac) VmxPage[num_cpus];
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;
        fbl::Array<VmxPage> pages(pages_ptr, num_cpus);
        VmxInfo vmx_info;
        for (auto& page : pages) {
            zx_status_t status = page.Alloc(vmx_info, 0);
            if (status != ZX_OK)
                return status;
        }

        // Enable VMX for all online CPUs.
        cpu_mask_t cpu_mask = percpu_exec(vmxon_task, &pages);
        if (cpu_mask != mp_get_online_mask()) {
            mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask, vmxoff_task, nullptr);
            return ZX_ERR_NOT_SUPPORTED;
        }

        vmxon_pages = fbl::move(pages);
    }
    num_guests++;
    return ZX_OK;
}

zx_status_t free_vmx_state() {
    fbl::AutoLock lock(&guest_mutex);
    num_guests--;
    if (num_guests == 0) {
        mp_sync_exec(MP_IPI_TARGET_ALL, 0, vmxoff_task, nullptr);
        vmxon_pages.reset();
    }
    return ZX_OK;
}

bool cr_is_invalid(uint64_t cr_value, uint32_t fixed0_msr, uint32_t fixed1_msr) {
    uint64_t fixed0 = read_msr(fixed0_msr);
    uint64_t fixed1 = read_msr(fixed1_msr);
    return ~(cr_value | ~fixed0) != 0 || ~(~cr_value | fixed1) != 0;
}
