// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmx_cpu_state_priv.h"

#include <assert.h>
#include <bits.h>
#include <string.h>

#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <vm/pmm.h>

#include <fbl/mutex.h>

static fbl::Mutex vmx_mutex;
static size_t num_vcpus TA_GUARDED(vmx_mutex) = 0;
static fbl::unique_ptr<VmxCpuState> vmx_cpu_state TA_GUARDED(vmx_mutex);

static mx_status_t vmxon(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmxon %[pa];" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        : [pa] "m"(pa)
        : "cc", "memory");

    return err ? MX_ERR_INTERNAL : MX_OK;
}

static mx_status_t vmxoff() {
    uint8_t err;

    __asm__ volatile(
        "vmxoff;" VMX_ERR_CHECK(err)
        : [err] "=r"(err)
        :
        : "cc");

    return err ? MX_ERR_INTERNAL : MX_OK;
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

MiscInfo::MiscInfo() {
    // From Volume 3, Appendix A.6.
    uint64_t misc_info = read_msr(X86_MSR_IA32_VMX_MISC);
    wait_for_sipi = BIT_SHIFT(misc_info, 8);
    msr_list_limit = static_cast<uint32_t>(BITS_SHIFT(misc_info, 27, 25) + 1) * 512;
}

EptInfo::EptInfo() {
    // From Volume 3, Appendix A.10.
    uint64_t ept_info = read_msr(X86_MSR_IA32_VMX_EPT_VPID_CAP);
    page_walk_4 = BIT_SHIFT(ept_info, 6);
    write_back = BIT_SHIFT(ept_info, 14);
    pde_2mb_page = BIT_SHIFT(ept_info, 16);
    pdpe_1gb_page = BIT_SHIFT(ept_info, 17);
    ept_flags = BIT_SHIFT(ept_info, 21);
    exit_info = BIT_SHIFT(ept_info, 22);
    invept =
        // INVEPT instruction is supported.
        BIT_SHIFT(ept_info, 20) &&
        // Single-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 25) &&
        // All-context INVEPT type is supported.
        BIT_SHIFT(ept_info, 26);
}

VmxPage::~VmxPage() {
    vm_page_t* page = paddr_to_vm_page(pa_);
    if (page != nullptr)
        pmm_free_page(page);
}

mx_status_t VmxPage::Alloc(const VmxInfo& vmx_info, uint8_t fill) {
    // From Volume 3, Appendix A.1: Bits 44:32 report the number of bytes that
    // software should allocate for the VMXON region and any VMCS region. It is
    // a value greater than 0 and at most 4096 (bit 44 is set if and only if
    // bits 43:32 are clear).
    if (vmx_info.region_size > PAGE_SIZE)
        return MX_ERR_NOT_SUPPORTED;

    // Check use of write-back memory for VMX regions is supported.
    if (!vmx_info.write_back)
        return MX_ERR_NOT_SUPPORTED;

    // The maximum size for a VMXON or VMCS region is 4096, therefore
    // unconditionally allocating a page is adequate.
    if (pmm_alloc_page(0, &pa_) == nullptr)
        return MX_ERR_NO_MEMORY;

    memset(VirtualAddress(), fill, PAGE_SIZE);
    return MX_OK;
}

paddr_t VmxPage::PhysicalAddress() const {
    DEBUG_ASSERT(pa_ != 0);
    return pa_;
}

void* VmxPage::VirtualAddress() const {
    DEBUG_ASSERT(pa_ != 0);
    return paddr_to_kvaddr(pa_);
}

static mx_status_t vmxon_task(void* context, uint cpu_num) {
    auto pages = static_cast<fbl::Array<VmxPage>*>(context);
    VmxPage& page = (*pages)[cpu_num];

    // Check that we have instruction information when we VM exit on IO.
    VmxInfo vmx_info;
    if (!vmx_info.io_exit_info)
        return MX_ERR_NOT_SUPPORTED;

    // Check that full VMX controls are supported.
    if (!vmx_info.vmx_controls)
        return MX_ERR_NOT_SUPPORTED;

    // Check that a page-walk length of 4 is supported.
    EptInfo ept_info;
    if (!ept_info.page_walk_4)
        return MX_ERR_NOT_SUPPORTED;

    // Check use write-back memory for EPT is supported.
    if (!ept_info.write_back)
        return MX_ERR_NOT_SUPPORTED;

    // Check that accessed and dirty flags for EPT are supported.
    if (!ept_info.ept_flags)
        return MX_ERR_NOT_SUPPORTED;

    // Check that the INVEPT instruction is supported.
    if (!ept_info.invept)
        return MX_ERR_NOT_SUPPORTED;

    // Check that wait for startup IPI is a supported activity state.
    MiscInfo misc_info;
    if (!misc_info.wait_for_sipi)
        return MX_ERR_NOT_SUPPORTED;

    // Enable VMXON, if required.
    uint64_t feature_control = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
    if (!(feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) ||
        !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
        if ((feature_control & X86_MSR_IA32_FEATURE_CONTROL_LOCK) &&
            !(feature_control & X86_MSR_IA32_FEATURE_CONTROL_VMXON)) {
            return MX_ERR_NOT_SUPPORTED;
        }
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_LOCK;
        feature_control |= X86_MSR_IA32_FEATURE_CONTROL_VMXON;
        write_msr(X86_MSR_IA32_FEATURE_CONTROL, feature_control);
    }

    // Check control registers are in a VMX-friendly state.
    uint64_t cr0 = x86_get_cr0();
    if (cr_is_invalid(cr0, X86_MSR_IA32_VMX_CR0_FIXED0, X86_MSR_IA32_VMX_CR0_FIXED1))
        return MX_ERR_BAD_STATE;
    uint64_t cr4 = x86_get_cr4() | X86_CR4_VMXE;
    if (cr_is_invalid(cr4, X86_MSR_IA32_VMX_CR4_FIXED0, X86_MSR_IA32_VMX_CR4_FIXED1))
        return MX_ERR_BAD_STATE;

    // Enable VMX using the VMXE bit.
    x86_set_cr4(cr4);

    // Setup VMXON page.
    VmxRegion* region = page.VirtualAddress<VmxRegion>();
    region->revision_id = vmx_info.revision_id;

    // Execute VMXON.
    mx_status_t status = vmxon(page.PhysicalAddress());
    if (status != MX_OK) {
        dprintf(CRITICAL, "Failed to turn on VMX on CPU %u\n", cpu_num);
        return status;
    }

    return MX_OK;
}

static void vmxoff_task(void* arg) {
    // Execute VMXOFF.
    mx_status_t status = vmxoff();
    if (status != MX_OK) {
        dprintf(CRITICAL, "Failed to turn off VMX on CPU %u\n", arch_curr_cpu_num());
        return;
    }

    // Disable VMX.
    x86_set_cr4(x86_get_cr4() & ~X86_CR4_VMXE);
}

// static
mx_status_t VmxCpuState::Create(fbl::unique_ptr<VmxCpuState>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<VmxCpuState> vmx_cpu_state(new (&ac) VmxCpuState);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    mx_status_t status = vmx_cpu_state->Init();
    if (status != MX_OK)
        return status;

    // Allocate a VMXON page for each CPU.
    size_t num_cpus = arch_max_num_cpus();
    VmxPage* pages = new (&ac) VmxPage[num_cpus];
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    fbl::Array<VmxPage> vmxon_pages(pages, num_cpus);
    VmxInfo vmx_info;
    for (auto& page : vmxon_pages) {
        mx_status_t status = page.Alloc(vmx_info, 0);
        if (status != MX_OK)
            return status;
    }

    // Enable VMX for all online CPUs.
    mp_cpu_mask_t cpu_mask = percpu_exec(vmxon_task, &vmxon_pages);
    if (cpu_mask != mp_get_online_mask()) {
        mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask, vmxoff_task, nullptr);
        return MX_ERR_NOT_SUPPORTED;
    }

    vmx_cpu_state->vmxon_pages_ = fbl::move(vmxon_pages);
    *out = fbl::move(vmx_cpu_state);
    return MX_OK;
}

VmxCpuState::~VmxCpuState() {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, vmxoff_task, nullptr);
}

mx_status_t alloc_vpid(uint16_t* vpid) {
    fbl::AutoLock lock(&vmx_mutex);
    if (num_vcpus == 0) {
        mx_status_t status = VmxCpuState::Create(&vmx_cpu_state);
        if (status != MX_OK)
            return status;
    }
    num_vcpus++;
    return vmx_cpu_state->AllocId(vpid);
}

mx_status_t free_vpid(uint16_t vpid) {
    fbl::AutoLock lock(&vmx_mutex);
    mx_status_t status = vmx_cpu_state->FreeId(vpid);
    if (status != MX_OK)
        return status;
    num_vcpus--;
    if (num_vcpus == 0)
        vmx_cpu_state.reset();
    return MX_OK;
}

bool cr_is_invalid(uint64_t cr_value, uint32_t fixed0_msr, uint32_t fixed1_msr) {
    uint64_t fixed0 = read_msr(fixed0_msr);
    uint64_t fixed1 = read_msr(fixed1_msr);
    return ~(cr_value | ~fixed0) != 0 || ~(~cr_value | fixed1) != 0;
}
