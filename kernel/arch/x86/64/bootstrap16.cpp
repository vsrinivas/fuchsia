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
#include <string.h>
#include <trace.h>

status_t x86_bootstrap16_prep(
        paddr_t bootstrap_phys_addr,
        uintptr_t entry64,
        vmm_aspace_t **temp_aspace,
        void **bootstrap_aperature)
{
    // Make sure bootstrap region will be entirely in the first 1MB of physical
    // memory
    if (bootstrap_phys_addr > (1 << 20) - 2 * PAGE_SIZE) {
        return ERR_INVALID_ARGS;
    }

    // Make sure the entrypoint code is in the bootstrap code that will be
    // loaded
    if (entry64 < (uintptr_t)x86_bootstrap16_start ||
        entry64 >= (uintptr_t)x86_bootstrap16_end) {
        return ERR_INVALID_ARGS;
    }

    vmm_aspace_t *bootstrap_aspace;
    vmm_aspace_t *kernel_aspace = vmm_get_kernel_aspace();
    void *bootstrap_virt_addr = NULL;
    status_t status = vmm_create_aspace(
            &bootstrap_aspace,
            "bootstrap16",
            VMM_ASPACE_TYPE_LOW_KERNEL);
    if (status != NO_ERROR) {
        return status;
    }

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
    for (unsigned int i = 0; i < countof(page_mappings); ++i) {
        void *vaddr = (void *)page_mappings[i].start_vaddr;
        status = vmm_alloc_physical(
                bootstrap_aspace,
                "bootstrap_mapping",
                page_mappings[i].size,
                &vaddr,
                PAGE_SIZE_SHIFT,
                0,
                page_mappings[i].start_paddr,
                VMM_FLAG_VALLOC_SPECIFIC,
                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
        if (status != NO_ERROR) {
            TRACEF("Failed to create wakeup bootstrap aspace\n");
            goto cleanup_aspace;
        }
    }

    // Map the AP bootstrap page and a low mem data page to configure
    // the AP processors with
    status = vmm_alloc_physical(
            kernel_aspace,
            "bootstrap16_aperture",
            PAGE_SIZE * 2, // size
            &bootstrap_virt_addr, // requested virtual address
            PAGE_SIZE_SHIFT, // alignment log2
            0, // min alloc gap
            bootstrap_phys_addr, // physical address
            0, // vmm flags
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE); // arch mmu flags
    if (status != NO_ERROR) {
        TRACEF("could not allocate AP bootstrap page: %d\n", status);
        goto cleanup_aspace;
    }
    DEBUG_ASSERT(bootstrap_virt_addr != NULL);
    uintptr_t bootstrap_code_len = (uintptr_t)x86_bootstrap16_end -
            (uintptr_t)x86_bootstrap16_start;
    DEBUG_ASSERT(bootstrap_code_len <= PAGE_SIZE);
    // Copy the bootstrap code in
    memcpy(bootstrap_virt_addr, x86_bootstrap16_start, bootstrap_code_len);

    // Configuration data shared with the APs to get them to 64-bit mode
    struct x86_bootstrap16_data *bootstrap_data = bootstrap_virt_addr + 0x1000;

    uint32_t long_mode_entry = bootstrap_phys_addr +
            (entry64 - (uintptr_t)x86_bootstrap16_start);

    bootstrap_data->phys_bootstrap_pml4 =
            vmm_get_arch_aspace(bootstrap_aspace)->pt_phys;
    bootstrap_data->phys_kernel_pml4 = x86_get_cr3();
    memcpy(bootstrap_data->phys_gdtr,
           &_gdtr_phys,
           sizeof(bootstrap_data->phys_gdtr));
    bootstrap_data->phys_long_mode_entry = long_mode_entry;
    bootstrap_data->long_mode_cs = CODE_64_SELECTOR;

    *bootstrap_aperature = bootstrap_virt_addr + 0x1000;
    *temp_aspace = bootstrap_aspace;
    return NO_ERROR;

cleanup_aspace:
    vmm_free_aspace(bootstrap_aspace);
    if (bootstrap_virt_addr) {
        vmm_free_region(kernel_aspace, (vaddr_t)bootstrap_virt_addr);
    }
    return status;
}
