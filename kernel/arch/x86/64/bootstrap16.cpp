// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <assert.h>
#include <err.h>
#include <vm/pmm.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <string.h>
#include <trace.h>

status_t x86_bootstrap16_prep(
        paddr_t bootstrap_phys_addr,
        uintptr_t entry64,
        fbl::RefPtr<VmAspace> *temp_aspace,
        void **bootstrap_aperature)
{
    // Make sure bootstrap region will be entirely in the first 1MB of physical
    // memory
    if (bootstrap_phys_addr > (1 << 20) - 2 * PAGE_SIZE) {
        return MX_ERR_INVALID_ARGS;
    }

    // Make sure the entrypoint code is in the bootstrap code that will be
    // loaded
    if (entry64 < (uintptr_t)x86_bootstrap16_start ||
        entry64 >= (uintptr_t)x86_bootstrap16_end) {
        return MX_ERR_INVALID_ARGS;
    }

    VmAspace *kernel_aspace = VmAspace::kernel_aspace();
    fbl::RefPtr<VmAspace> bootstrap_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL,
                                                               "bootstrap16");
    if (!bootstrap_aspace) {
        return MX_ERR_NO_MEMORY;
    }
    void *bootstrap_virt_addr = NULL;

    // add an auto caller to clean up the address space on the way out
    auto ac = fbl::MakeAutoCall([&]() {
        bootstrap_aspace->Destroy();
        if (bootstrap_virt_addr) {
            kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(bootstrap_virt_addr));
        }
    });

    // GDTR referring to identity-mapped gdt
    extern uint8_t _gdtr_phys;
    // Actual GDT address, needed for computation below
    extern uint8_t _gdt;
    extern uint8_t _gdt_end;

    // Compute what needs to go into the mappings
    paddr_t gdt_phys_page =
            vaddr_to_paddr((void *)ROUNDDOWN((uintptr_t)&_gdt, PAGE_SIZE));
    uintptr_t gdt_region_len =
            ROUNDUP((uintptr_t)&_gdt_end, PAGE_SIZE) - ROUNDDOWN((uintptr_t)&_gdt, PAGE_SIZE);

    // Temporary aspace needs 5 regions mapped:
    struct map_range page_mappings[] = {
        // 1) The bootstrap code page (identity mapped)
        // 2) The bootstrap data page (identity mapped)
        { .start_vaddr = bootstrap_phys_addr, .start_paddr = bootstrap_phys_addr, .size = 2 * PAGE_SIZE },
        // 3) The page containing the GDT (identity mapped)
        { .start_vaddr = (vaddr_t)gdt_phys_page, .start_paddr = gdt_phys_page, .size = gdt_region_len },
        // These next two come implicitly from the shared kernel aspace:
        // 4) The kernel's version of the bootstrap code page (matched mapping)
        // 5) The page containing the aps_still_booting counter (matched mapping)
    };
    for (unsigned int i = 0; i < fbl::count_of(page_mappings); ++i) {
        void *vaddr = (void *)page_mappings[i].start_vaddr;
        status_t status = bootstrap_aspace->AllocPhysical(
                "bootstrap_mapping",
                page_mappings[i].size,
                &vaddr,
                PAGE_SIZE_SHIFT,
                page_mappings[i].start_paddr,
                VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
        if (status != MX_OK) {
            TRACEF("Failed to create wakeup bootstrap aspace\n");
            return status;
        }
    }

    // Map the AP bootstrap page and a low mem data page to configure
    // the AP processors with
    status_t status = kernel_aspace->AllocPhysical(
            "bootstrap16_aperture",
            PAGE_SIZE * 2, // size
            &bootstrap_virt_addr, // requested virtual address
            PAGE_SIZE_SHIFT, // alignment log2
            bootstrap_phys_addr, // physical address
            0, // vmm flags
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE); // arch mmu flags
    if (status != MX_OK) {
        TRACEF("could not allocate AP bootstrap page: %d\n", status);
        return status;
    }
    DEBUG_ASSERT(bootstrap_virt_addr != NULL);
    uintptr_t bootstrap_code_len = (uintptr_t)x86_bootstrap16_end -
            (uintptr_t)x86_bootstrap16_start;
    DEBUG_ASSERT(bootstrap_code_len <= PAGE_SIZE);
    // Copy the bootstrap code in
    memcpy(bootstrap_virt_addr, (const void *)x86_bootstrap16_start, bootstrap_code_len);

    // Configuration data shared with the APs to get them to 64-bit mode
    struct x86_bootstrap16_data *bootstrap_data =
        (struct x86_bootstrap16_data *)((uintptr_t)bootstrap_virt_addr + 0x1000);

    uintptr_t long_mode_entry = bootstrap_phys_addr +
            (entry64 - (uintptr_t)x86_bootstrap16_start);
    ASSERT(long_mode_entry <= UINT32_MAX);

    uint64_t phys_bootstrap_pml4 = bootstrap_aspace->arch_aspace().pt_phys();
    uint64_t phys_kernel_pml4 = x86_get_cr3();
    if (phys_bootstrap_pml4 > UINT32_MAX) {
        // TODO(MG-978): Once the pmm supports it, we should request that this
        // VmAspace is backed by a low mem PML4, so we can avoid this issue.
        TRACEF("bootstrap PML4 was not allocated out of low mem\n");
        return MX_ERR_NO_MEMORY;
    }
    ASSERT(phys_kernel_pml4 <= UINT32_MAX);

    bootstrap_data->phys_bootstrap_pml4 = static_cast<uint32_t>(phys_bootstrap_pml4);
    bootstrap_data->phys_kernel_pml4 = static_cast<uint32_t>(phys_kernel_pml4);
    memcpy(bootstrap_data->phys_gdtr,
           &_gdtr_phys,
           sizeof(bootstrap_data->phys_gdtr));
    bootstrap_data->phys_long_mode_entry = static_cast<uint32_t>(long_mode_entry);
    bootstrap_data->long_mode_cs = CODE_64_SELECTOR;

    *bootstrap_aperature = (void *)((uintptr_t)bootstrap_virt_addr + 0x1000);
    *temp_aspace = bootstrap_aspace;

    // cancel the cleanup autocall, since we're returning the new aspace and region
    ac.cancel();

    return MX_OK;
}
