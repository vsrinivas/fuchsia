// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm_priv.h"
#include <arch/mmu.h>
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <lib/console.h>
#include <lk/init.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

extern int _start;
extern int _end;

extern int __code_start;
extern int __code_end;
extern int __rodata_start;
extern int __rodata_end;
extern int __data_start;
extern int __data_end;
extern int __bss_start;
extern int __bss_end;

// mark the physical pages backing a range of virtual as in use.
// allocate the physical pages and throw them away
static void mark_pages_in_use(vaddr_t va, size_t len) {
    LTRACEF("va %#" PRIxPTR ", len %#zx\n", va, len);

    // make sure we are inclusive of all of the pages in the address range
    len = PAGE_ALIGN(len + (va & (PAGE_SIZE - 1)));
    va = ROUNDDOWN(va, PAGE_SIZE);

    LTRACEF("aligned va %#" PRIxPTR ", len 0x%zx\n", va, len);

    for (size_t offset = 0; offset < len; offset += PAGE_SIZE) {
        uint flags;
        paddr_t pa;

        status_t err = arch_mmu_query(&vmm_aspace_to_obj(vmm_get_kernel_aspace())->arch_aspace(),
                                      va + offset, &pa, &flags);
        if (err >= 0) {
            // LTRACEF("va 0x%x, pa 0x%x, flags 0x%x, err %d\n", va + offset, pa, flags, err);

            // alloate the range, throw the results away
            pmm_alloc_range(pa, 1, nullptr);
        } else {
            panic("Could not find pa for va %#" PRIxPTR "\n", va);
        }
    }
}

void vm_init_preheap(uint level) {
    LTRACE_ENTRY;

    // allow the vmm a shot at initializing some of its data structures
    VmAspace::KernelAspaceInitPreHeap();

    // mark all of the kernel pages in use
    LTRACEF("marking all kernel pages as used\n");
    mark_pages_in_use((vaddr_t)&_start, ((uintptr_t)&_end - (uintptr_t)&_start));

    // mark the physical pages used by the boot time allocator
    if (boot_alloc_end != boot_alloc_start) {
        LTRACEF("marking boot alloc used from %#" PRIxPTR " to %#" PRIxPTR "\n",
                boot_alloc_start, boot_alloc_end);

        mark_pages_in_use(boot_alloc_start, boot_alloc_end - boot_alloc_start);
    }
}

void vm_init_postheap(uint level) {
    LTRACE_ENTRY;

    vmm_aspace_t* aspace = vmm_get_kernel_aspace();

    // we expect the kernel to be in a temporary mapping, define permanent
    // regions for those now
    struct temp_region {
        const char* name;
        vaddr_t base;
        size_t size;
        uint arch_mmu_flags;
    } regions[] = {
        {
            .name = "kernel_code",
            .base = (vaddr_t)&__code_start,
            .size = ROUNDUP((size_t)&__code_end - (size_t)&__code_start, PAGE_SIZE),
            .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE,
        },
        {
            .name = "kernel_rodata",
            .base = (vaddr_t)&__rodata_start,
            .size = ROUNDUP((size_t)&__rodata_end - (size_t)&__rodata_start, PAGE_SIZE),
            .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ,
        },
        {
            .name = "kernel_data",
            .base = (vaddr_t)&__data_start,
            .size = ROUNDUP((size_t)&__data_end - (size_t)&__data_start, PAGE_SIZE),
            .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
        },
        {
            .name = "kernel_bss",
            .base = (vaddr_t)&__bss_start,
            .size = ROUNDUP((size_t)&__bss_end - (size_t)&__bss_start, PAGE_SIZE),
            .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
        },
        {
            .name = "kernel_bootalloc",
            .base = (vaddr_t)boot_alloc_start,
            .size = ROUNDUP(boot_alloc_end - boot_alloc_start, PAGE_SIZE),
            .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
        },
    };

    for (uint i = 0; i < countof(regions); ++i) {
        temp_region* region = &regions[i];
        ASSERT(IS_PAGE_ALIGNED(region->base));
        status_t status = vmm_reserve_space(aspace, region->name, region->size, region->base);
        ASSERT(status == NO_ERROR);
        status = vmm_protect_region(aspace, region->base, region->arch_mmu_flags);
        ASSERT(status == NO_ERROR);
    }

    // mmu_initial_mappings should reflect where we are now, use it to construct the actual
    // mappings.  We will carve out the kernel code/data from any mappings and
    // unmap any temporary ones.
    const struct mmu_initial_mapping* map = mmu_initial_mappings;
    for (map = mmu_initial_mappings; map->size > 0; ++map) {
        LTRACEF("looking at mapping %p (%s)\n", map, map->name);
        // Unmap temporary mappings except where they intersect with the
        // kernel code/data regions.
        vaddr_t vaddr = map->virt;
        LTRACEF("vaddr %#" PRIxPTR ", virt + size %#" PRIxPTR "\n", vaddr, map->virt + map->size);
        while (vaddr != map->virt + map->size) {
            vaddr_t next_kernel_region = map->virt + map->size;
            vaddr_t next_kernel_region_end = map->virt + map->size;

            // Find the kernel code/data region with the lowest start address
            // that is within this mapping.
            for (uint i = 0; i < countof(regions); ++i) {
                temp_region* region = &regions[i];

                if (region->base >= vaddr && region->base < map->virt + map->size &&
                    region->base < next_kernel_region) {

                    next_kernel_region = region->base;
                    next_kernel_region_end = region->base + region->size;
                }
            }

            // If vaddr isn't the start of a kernel code/data region, then we should make
            // a mapping between it and the next closest one.
            if (next_kernel_region != vaddr) {
                status_t status =
                    vmm_reserve_space(aspace, map->name, next_kernel_region - vaddr, vaddr);
                ASSERT(status == NO_ERROR);

                if (map->flags & MMU_INITIAL_MAPPING_TEMPORARY) {
                    // If the region is part of a temporary mapping, immediately unmap it
                    LTRACEF("Freeing region [%016" PRIxPTR ", %016" PRIxPTR
                            ")\n", vaddr, next_kernel_region);
                    status = vmm_free_region(aspace, vaddr);
                    ASSERT(status == NO_ERROR);
                } else {
                    // Otherwise, mark it no-exec since it's not explicitly code
                    status = vmm_protect_region(
                        aspace,
                        vaddr,
                        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
                    ASSERT(status == NO_ERROR);
                }
            }
            vaddr = next_kernel_region_end;
        }
    }
}

void* paddr_to_kvaddr(paddr_t pa) {
    // slow path to do reverse lookup
    struct mmu_initial_mapping* map = mmu_initial_mappings;
    while (map->size > 0) {
        if (!(map->flags & MMU_INITIAL_MAPPING_TEMPORARY) && pa >= map->phys &&
            pa <= map->phys + map->size - 1) {
            return (void*)(map->virt + (pa - map->phys));
        }
        map++;
    }
    return nullptr;
}

paddr_t vaddr_to_paddr(const void* ptr) {
    vmm_aspace_t* _aspace = vaddr_to_aspace(ptr);
    if (!_aspace)
        return (paddr_t) nullptr;

    VmAspace* aspace = vmm_aspace_to_obj(_aspace);

    paddr_t pa;
    status_t rc = arch_mmu_query(&aspace->arch_aspace(), (vaddr_t)ptr, &pa, nullptr);
    if (rc)
        return (paddr_t) nullptr;

    return pa;
}

vmm_aspace_t* vaddr_to_aspace(const void* ptr) {
    if (is_kernel_address((vaddr_t)ptr)) {
        return vmm_get_kernel_aspace();
    } else if (is_user_address((vaddr_t)ptr)) {
        return get_current_thread()->aspace;
    } else {
        return nullptr;
    }
}

static int cmd_vm(int argc, const cmd_args* argv) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s phys2virt <address>\n", argv[0].str);
        printf("%s virt2phys <address>\n", argv[0].str);
        printf("%s map <phys> <virt> <count> <flags>\n", argv[0].str);
        printf("%s unmap <virt> <count>\n", argv[0].str);
        return ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "phys2virt")) {
        if (argc < 3)
            goto notenoughargs;

        void* ptr = paddr_to_kvaddr((paddr_t)argv[2].u);
        printf("paddr_to_kvaddr returns %p\n", ptr);
    } else if (!strcmp(argv[1].str, "virt2phys")) {
        if (argc < 3)
            goto notenoughargs;

        vmm_aspace_t* _aspace = vaddr_to_aspace((void*)argv[2].u);
        if (!_aspace) {
            printf("ERROR: outside of any address space\n");
            return -1;
        }

        VmAspace* aspace = vmm_aspace_to_obj(_aspace);

        paddr_t pa;
        uint flags;
        status_t err = arch_mmu_query(&aspace->arch_aspace(), argv[2].u, &pa, &flags);
        printf("arch_mmu_query returns %d\n", err);
        if (err >= 0) {
            printf("\tpa %#" PRIxPTR ", flags %#x\n", pa, flags);
        }
    } else if (!strcmp(argv[1].str, "map")) {
        if (argc < 6)
            goto notenoughargs;

        vmm_aspace_t* _aspace = vaddr_to_aspace((void*)argv[2].u);
        if (!_aspace) {
            printf("ERROR: outside of any address space\n");
            return -1;
        }

        VmAspace* aspace = vmm_aspace_to_obj(_aspace);

        int err = arch_mmu_map(&aspace->arch_aspace(), argv[3].u, argv[2].u, (uint)argv[4].u,
                               (uint)argv[5].u);
        printf("arch_mmu_map returns %d\n", err);
    } else if (!strcmp(argv[1].str, "unmap")) {
        if (argc < 4)
            goto notenoughargs;

        vmm_aspace_t* _aspace = vaddr_to_aspace((void*)argv[2].u);
        if (!_aspace) {
            printf("ERROR: outside of any address space\n");
            return -1;
        }

        VmAspace* aspace = vmm_aspace_to_obj(_aspace);

        int err = arch_mmu_unmap(&aspace->arch_aspace(), argv[2].u, (uint)argv[3].u);
        printf("arch_mmu_unmap returns %d\n", err);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vm", "vm commands", &cmd_vm)
#endif
STATIC_COMMAND_END(vm);

LK_INIT_HOOK(vm_preheap, &vm_init_preheap, LK_INIT_LEVEL_HEAP - 1);
LK_INIT_HOOK(vm, &vm_init_postheap, LK_INIT_LEVEL_VM);
