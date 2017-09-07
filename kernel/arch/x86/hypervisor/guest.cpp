// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <hypervisor/guest_physical_address_space.h>
#include <magenta/syscalls/hypervisor.h>

#include "vmx_cpu_state_priv.h"

static const mx_vaddr_t kIoApicPhysBase = 0xfec00000;

static void ignore_msr(VmxPage* msr_bitmaps_page, uint32_t msr) {
    // From Volume 3, Section 24.6.9.
    uint8_t* msr_bitmaps = msr_bitmaps_page->VirtualAddress<uint8_t>();
    if (msr >= 0xc0000000)
        msr_bitmaps += 1 << 10;

    uint16_t msr_low = msr & 0x1fff;
    uint16_t msr_byte = msr_low / 8;
    uint8_t msr_bit = msr_low % 8;

    // Ignore reads to the MSR.
    msr_bitmaps[msr_byte] &= (uint8_t) ~(1 << msr_bit);

    // Ignore writes to the MSR.
    msr_bitmaps += 2 << 10;
    msr_bitmaps[msr_byte] &= (uint8_t) ~(1 << msr_bit);
}

// static
mx_status_t Guest::Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Guest> guest(new (&ac) Guest);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    mx_status_t status = GuestPhysicalAddressSpace::Create(fbl::move(physmem), &guest->gpas_);
    if (status != MX_OK)
        return status;

    // We ensure the page containing the IO APIC address is not mapped so that
    // we VM exit with an EPT violation when the guest accesses the page.
    status = guest->gpas_->UnmapRange(kIoApicPhysBase, PAGE_SIZE);
    if (status != MX_OK)
        return status;

    // Setup common APIC access.
    VmxInfo vmx_info;
    status = guest->apic_access_page_.Alloc(vmx_info, 0);
    if (status != MX_OK)
        return status;

    status = guest->gpas_->MapApicPage(APIC_PHYS_BASE,
                                       guest->apic_access_page_.PhysicalAddress());
    if (status != MX_OK)
        return status;

    // Setup common MSR bitmaps.
    status = guest->msr_bitmaps_page_.Alloc(vmx_info, UINT8_MAX);
    if (status != MX_OK)
        return status;

    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_PAT);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_EFER);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_FS_BASE);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_GS_BASE);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_KERNEL_GS_BASE);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_STAR);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_LSTAR);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_FMASK);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_TSC_ADJUST);
    ignore_msr(&guest->msr_bitmaps_page_, X86_MSR_IA32_TSC_AUX);

    *out = fbl::move(guest);
    return MX_OK;
}

Guest::~Guest() {
    __UNUSED mx_status_t status = gpas_->UnmapRange(APIC_PHYS_BASE, PAGE_SIZE);
    DEBUG_ASSERT(status == MX_OK);
}

mx_status_t Guest::SetTrap(uint32_t kind, mx_vaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    if (len == 0)
        return MX_ERR_INVALID_ARGS;
    if (SIZE_MAX - len < addr)
        return MX_ERR_OUT_OF_RANGE;
    switch (kind) {
    case MX_GUEST_TRAP_MEM:
        if (!IS_PAGE_ALIGNED(addr) || !IS_PAGE_ALIGNED(len))
            return MX_ERR_INVALID_ARGS;
        return gpas_->UnmapRange(addr, len);
    case MX_GUEST_TRAP_IO:
        if (addr + len > UINT16_MAX)
            return MX_ERR_OUT_OF_RANGE;
        return mux_.AddPortRange(addr, len, fbl::move(port), key);
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

mx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest) {
    // Check that the CPU supports VMX.
    if (!x86_feature_test(X86_FEATURE_VMX))
        return MX_ERR_NOT_SUPPORTED;

    return Guest::Create(fbl::move(physmem), guest);
}

mx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, mx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key) {
    return guest->SetTrap(kind, addr, len, port, key);
}
