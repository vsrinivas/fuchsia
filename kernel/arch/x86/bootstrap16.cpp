// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/mutex.h>
#include <string.h>
#include <trace.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

static paddr_t bootstrap_phys_addr = UINT64_MAX;
static fbl::Mutex bootstrap_lock;

void x86_bootstrap16_init(paddr_t bootstrap_base) {
    DEBUG_ASSERT(!IS_PAGE_ALIGNED(bootstrap_phys_addr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(bootstrap_base));
    DEBUG_ASSERT(bootstrap_base <= (1024 * 1024) - 2 * PAGE_SIZE);
    bootstrap_phys_addr = bootstrap_base;
}

zx_status_t x86_bootstrap16_acquire(uintptr_t entry64, fbl::RefPtr<VmAspace>* temp_aspace,
                                    void** bootstrap_aperature, paddr_t* instr_ptr)
    TA_NO_THREAD_SAFETY_ANALYSIS {
    // Make sure x86_bootstrap16_init has been called, and bail early if not.
    if (!IS_PAGE_ALIGNED(bootstrap_phys_addr)) {
        return ZX_ERR_BAD_STATE;
    }

    // Make sure the entrypoint code is in the bootstrap code that will be
    // loaded
    if (entry64 < (uintptr_t)x86_bootstrap16_start ||
        entry64 >= (uintptr_t)x86_bootstrap16_end) {
        return ZX_ERR_INVALID_ARGS;
    }

    VmAspace* kernel_aspace = VmAspace::kernel_aspace();
    fbl::RefPtr<VmAspace> bootstrap_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL,
                                                              "bootstrap16");
    if (!bootstrap_aspace) {
        return ZX_ERR_NO_MEMORY;
    }
    void* bootstrap_virt_addr = nullptr;

    // Ensure only one caller is using the bootstrap region
    bootstrap_lock.Acquire();

    // add an auto caller to clean up the address space on the way out
    auto ac = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        bootstrap_aspace->Destroy();
        if (bootstrap_virt_addr) {
            kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(bootstrap_virt_addr));
        }
        bootstrap_lock.Release();
    });

    // Actual GDT address.
    extern uint8_t _gdt;
    extern uint8_t _gdt_end;

    // Compute what needs to go into the mappings
    paddr_t gdt_phys_page =
        vaddr_to_paddr((void*)ROUNDDOWN((uintptr_t)&_gdt, PAGE_SIZE));
    uintptr_t gdt_region_len =
        ROUNDUP((uintptr_t)&_gdt_end, PAGE_SIZE) - ROUNDDOWN((uintptr_t)&_gdt, PAGE_SIZE);

    // Temporary aspace needs 5 regions mapped:
    struct map_range page_mappings[] = {
        // 1) The bootstrap code page (identity mapped)
        // 2) The bootstrap data page (identity mapped)
        {.start_vaddr = bootstrap_phys_addr, .start_paddr = bootstrap_phys_addr, .size = 2 * PAGE_SIZE},
        // 3) The page containing the GDT (identity mapped)
        {.start_vaddr = (vaddr_t)gdt_phys_page, .start_paddr = gdt_phys_page, .size = gdt_region_len},
        // These next two come implicitly from the shared kernel aspace:
        // 4) The kernel's version of the bootstrap code page (matched mapping)
        // 5) The page containing the aps_still_booting counter (matched mapping)
    };
    for (unsigned int i = 0; i < fbl::count_of(page_mappings); ++i) {
        void* vaddr = (void*)page_mappings[i].start_vaddr;
        zx_status_t status = bootstrap_aspace->AllocPhysical(
            "bootstrap_mapping",
            page_mappings[i].size,
            &vaddr,
            PAGE_SIZE_SHIFT,
            page_mappings[i].start_paddr,
            VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
        if (status != ZX_OK) {
            TRACEF("Failed to create wakeup bootstrap aspace\n");
            return status;
        }
    }

    // Map the AP bootstrap page and a low mem data page to configure
    // the AP processors with
    zx_status_t status = kernel_aspace->AllocPhysical(
        "bootstrap16_aperture",
        PAGE_SIZE * 2,                                       // size
        &bootstrap_virt_addr,                                // requested virtual address
        PAGE_SIZE_SHIFT,                                     // alignment log2
        bootstrap_phys_addr,                                 // physical address
        0,                                                   // vmm flags
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE); // arch mmu flags
    if (status != ZX_OK) {
        TRACEF("could not allocate AP bootstrap page: %d\n", status);
        return status;
    }
    DEBUG_ASSERT(bootstrap_virt_addr != nullptr);
    uintptr_t bootstrap_code_len = (uintptr_t)x86_bootstrap16_end -
                                   (uintptr_t)x86_bootstrap16_start;
    DEBUG_ASSERT(bootstrap_code_len <= PAGE_SIZE);
    // Copy the bootstrap code in
    memcpy(bootstrap_virt_addr, (const void*)x86_bootstrap16_start, bootstrap_code_len);

    // Configuration data shared with the APs to get them to 64-bit mode
    struct x86_bootstrap16_data* bootstrap_data =
        (struct x86_bootstrap16_data*)((uintptr_t)bootstrap_virt_addr + 0x1000);

    uintptr_t long_mode_entry = bootstrap_phys_addr +
                                (entry64 - (uintptr_t)x86_bootstrap16_start);
    ASSERT(long_mode_entry <= UINT32_MAX);

    uint64_t phys_bootstrap_pml4 = bootstrap_aspace->arch_aspace().pt_phys();
    uint64_t phys_kernel_pml4 = VmAspace::kernel_aspace()->arch_aspace().pt_phys();
    if (phys_bootstrap_pml4 > UINT32_MAX) {
        // TODO(ZX-978): Once the pmm supports it, we should request that this
        // VmAspace is backed by a low mem PML4, so we can avoid this issue.
        TRACEF("bootstrap PML4 was not allocated out of low mem\n");
        return ZX_ERR_NO_MEMORY;
    }
    ASSERT(phys_kernel_pml4 <= UINT32_MAX);

    bootstrap_data->phys_bootstrap_pml4 = static_cast<uint32_t>(phys_bootstrap_pml4);
    bootstrap_data->phys_kernel_pml4 = static_cast<uint32_t>(phys_kernel_pml4);
    bootstrap_data->phys_gdtr_limit =
        static_cast<uint16_t>(&_gdt_end - &_gdt - 1);
    bootstrap_data->phys_gdtr_base =
        reinterpret_cast<uintptr_t>(&_gdt) -
        reinterpret_cast<uintptr_t>(__code_start) +
        get_kernel_base_phys();
    bootstrap_data->phys_long_mode_entry = static_cast<uint32_t>(long_mode_entry);
    bootstrap_data->long_mode_cs = CODE_64_SELECTOR;

    *bootstrap_aperature = (void*)((uintptr_t)bootstrap_virt_addr + 0x1000);
    *temp_aspace = bootstrap_aspace;
    *instr_ptr = bootstrap_phys_addr;

    // Cancel the cleanup autocall, since we're returning the new aspace and region
    // NOTE: Since we cancel the autocall, we are not releasing
    // |bootstrap_lock|.  This is released in x86_bootstrap16_release() when the
    // caller is done with the bootstrap region.
    ac.cancel();

    return ZX_OK;
}

void x86_bootstrap16_release(void* bootstrap_aperature) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(bootstrap_aperature);
    DEBUG_ASSERT(bootstrap_lock.IsHeld());
    VmAspace* kernel_aspace = VmAspace::kernel_aspace();
    uintptr_t addr = reinterpret_cast<uintptr_t>(bootstrap_aperature) - 0x1000;
    kernel_aspace->FreeRegion(addr);

    bootstrap_lock.Release();
}
