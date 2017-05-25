// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <lib/heap.h>
#include <rand.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#define LOCAL_TRACE 0
#define TRACE_CONTEXT_SWITCH 0

static_assert(((long)KERNEL_BASE >> MMU_KERNEL_SIZE_SHIFT) == -1, "");
static_assert(((long)KERNEL_ASPACE_BASE >> MMU_KERNEL_SIZE_SHIFT) == -1, "");
static_assert(MMU_KERNEL_SIZE_SHIFT <= 48, "");
static_assert(MMU_KERNEL_SIZE_SHIFT >= 25, "");

static uint64_t asid_pool[(1 << MMU_ARM64_ASID_BITS) / 64];
static mutex_t asid_lock = MUTEX_INITIAL_VALUE(asid_lock);

uint32_t arm64_zva_shift;

/* the main translation table */
pte_t arm64_kernel_translation_table[MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP] __ALIGNED(MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP * 8)
    __SECTION(".bss.prebss.translation_table");

static status_t arm64_mmu_alloc_asid(uint16_t* asid) {

    uint16_t new_asid;
    uint32_t retry = 1 << MMU_ARM64_ASID_BITS;

    mutex_acquire(&asid_lock);
    do {
        new_asid = static_cast<uint16_t>(rand()) & ~(-(1 << MMU_ARM64_ASID_BITS));
        retry--;
        if (retry == 0) {
            mutex_release(&asid_lock);
            return ERR_NO_MEMORY;
        }
    } while ((asid_pool[new_asid >> 6] & (1 << (new_asid % 64))) || (new_asid == 0));

    asid_pool[new_asid >> 6] = asid_pool[new_asid >> 6] | (1 << (new_asid % 64));

    mutex_release(&asid_lock);

    *asid = new_asid;

    return NO_ERROR;
}

static status_t arm64_mmu_free_asid(uint16_t asid) {

    mutex_acquire(&asid_lock);

    asid_pool[asid >> 6] = asid_pool[asid >> 6] & ~(1 << (asid % 64));

    mutex_release(&asid_lock);

    return NO_ERROR;
}

static inline bool is_valid_vaddr(arch_aspace_t* aspace, vaddr_t vaddr) {
    return (vaddr >= aspace->base && vaddr <= aspace->base + aspace->size - 1);
}

/* convert user level mmu flags to flags that go in L1 descriptors */
static pte_t mmu_flags_to_pte_attr(uint flags) {
    pte_t attr = MMU_PTE_ATTR_AF;

    switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
    case ARCH_MMU_FLAG_CACHED:
        attr |= MMU_PTE_ATTR_NORMAL_MEMORY | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
        break;
    case ARCH_MMU_FLAG_WRITE_COMBINING:
    case ARCH_MMU_FLAG_UNCACHED:
        attr |= MMU_PTE_ATTR_STRONGLY_ORDERED;
        break;
    case ARCH_MMU_FLAG_UNCACHED_DEVICE:
        attr |= MMU_PTE_ATTR_DEVICE;
        break;
    default:
        /* invalid user-supplied flag */
        DEBUG_ASSERT(1);
        return ERR_INVALID_ARGS;
    }

    switch (flags & (ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE)) {
    case 0:
        attr |= MMU_PTE_ATTR_AP_P_RO_U_NA;
        break;
    case ARCH_MMU_FLAG_PERM_WRITE:
        attr |= MMU_PTE_ATTR_AP_P_RW_U_NA;
        break;
    case ARCH_MMU_FLAG_PERM_USER:
        attr |= MMU_PTE_ATTR_AP_P_RO_U_RO;
        break;
    case ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE:
        attr |= MMU_PTE_ATTR_AP_P_RW_U_RW;
        break;
    }

    if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
        attr |= MMU_PTE_ATTR_UXN | MMU_PTE_ATTR_PXN;
    }

    if (flags & ARCH_MMU_FLAG_NS) {
        attr |= MMU_PTE_ATTR_NON_SECURE;
    }

    return attr;
}

status_t arch_mmu_query(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t* paddr, uint* flags) {
    ulong index;
    uint index_shift;
    uint page_size_shift;
    pte_t pte;
    pte_t pte_addr;
    uint descriptor_type;
    volatile pte_t* page_table;
    vaddr_t vaddr_rem;

    LTRACEF("aspace %p, vaddr 0x%lx\n", aspace, vaddr);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
    DEBUG_ASSERT(aspace->tt_virt);

    DEBUG_ASSERT(is_valid_vaddr(aspace, vaddr));
    if (!is_valid_vaddr(aspace, vaddr))
        return ERR_OUT_OF_RANGE;

    /* compute shift values based on if this address space is for kernel or user space */
    if (aspace->flags & ARCH_ASPACE_FLAG_KERNEL) {
        index_shift = MMU_KERNEL_TOP_SHIFT;
        page_size_shift = MMU_KERNEL_PAGE_SIZE_SHIFT;

        vaddr_t kernel_base = ~0UL << MMU_KERNEL_SIZE_SHIFT;
        vaddr_rem = vaddr - kernel_base;

        index = vaddr_rem >> index_shift;
        ASSERT(index < MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP);
    } else {
        index_shift = MMU_USER_TOP_SHIFT;
        page_size_shift = MMU_USER_PAGE_SIZE_SHIFT;

        vaddr_rem = vaddr;
        index = vaddr_rem >> index_shift;
        ASSERT(index < MMU_USER_PAGE_TABLE_ENTRIES_TOP);
    }

    page_table = aspace->tt_virt;

    while (true) {
        index = vaddr_rem >> index_shift;
        vaddr_rem -= (vaddr_t)index << index_shift;
        pte = page_table[index];
        descriptor_type = pte & MMU_PTE_DESCRIPTOR_MASK;
        pte_addr = pte & MMU_PTE_OUTPUT_ADDR_MASK;

        LTRACEF("va %#" PRIxPTR ", index %lu, index_shift %u, rem %#" PRIxPTR
                ", pte %#" PRIx64 "\n",
                vaddr, index, index_shift, vaddr_rem, pte);

        if (descriptor_type == MMU_PTE_DESCRIPTOR_INVALID)
            return ERR_NOT_FOUND;

        if (descriptor_type == ((index_shift > page_size_shift) ? MMU_PTE_L012_DESCRIPTOR_BLOCK : MMU_PTE_L3_DESCRIPTOR_PAGE)) {
            break;
        }

        if (index_shift <= page_size_shift ||
            descriptor_type != MMU_PTE_L012_DESCRIPTOR_TABLE) {
            PANIC_UNIMPLEMENTED;
        }

        page_table = static_cast<volatile pte_t*>(paddr_to_kvaddr(pte_addr));
        index_shift -= page_size_shift - 3;
    }

    if (paddr)
        *paddr = pte_addr + vaddr_rem;
    if (flags) {
        *flags = 0;
        if (pte & MMU_PTE_ATTR_NON_SECURE)
            *flags |= ARCH_MMU_FLAG_NS;
        switch (pte & MMU_PTE_ATTR_ATTR_INDEX_MASK) {
        case MMU_PTE_ATTR_STRONGLY_ORDERED:
            *flags |= ARCH_MMU_FLAG_UNCACHED;
            break;
        case MMU_PTE_ATTR_DEVICE:
            *flags |= ARCH_MMU_FLAG_UNCACHED_DEVICE;
            break;
        case MMU_PTE_ATTR_NORMAL_MEMORY:
            break;
        default:
            PANIC_UNIMPLEMENTED;
        }
        *flags |= ARCH_MMU_FLAG_PERM_READ;
        switch (pte & MMU_PTE_ATTR_AP_MASK) {
        case MMU_PTE_ATTR_AP_P_RW_U_NA:
            *flags |= ARCH_MMU_FLAG_PERM_WRITE;
            break;
        case MMU_PTE_ATTR_AP_P_RW_U_RW:
            *flags |= ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE;
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_NA:
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_RO:
            *flags |= ARCH_MMU_FLAG_PERM_USER;
            break;
        }
        if (!((pte & MMU_PTE_ATTR_UXN) && (pte & MMU_PTE_ATTR_PXN))) {
            *flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
        }
    }
    LTRACEF("va 0x%lx, paddr 0x%lx, flags 0x%x\n",
            vaddr, paddr ? *paddr : ~0UL, flags ? *flags : ~0U);
    return 0;
}

static status_t alloc_page_table(paddr_t* paddrp, uint page_size_shift) {
    size_t size = 1UL << page_size_shift;

    DEBUG_ASSERT(page_size_shift <= MMU_MAX_PAGE_SIZE_SHIFT);

    LTRACEF("page_size_shift %u\n", page_size_shift);

    if (size >= PAGE_SIZE) {
        size_t count = size / PAGE_SIZE;
        size_t ret = pmm_alloc_contiguous(count, PMM_ALLOC_FLAG_KMAP,
                                          static_cast<uint8_t>(page_size_shift), paddrp, NULL);
        if (ret != count)
            return ERR_NO_MEMORY;
    } else if (size == PAGE_SIZE) {
        void* vaddr = pmm_alloc_kpage(paddrp, NULL);
        if (!vaddr)
            return ERR_NO_MEMORY;
    } else {
        void* vaddr = memalign(size, size);
        if (!vaddr)
            return ERR_NO_MEMORY;
        *paddrp = vaddr_to_paddr(vaddr);
        if (*paddrp == 0) {
            free(vaddr);
            return ERR_NO_MEMORY;
        }
    }

    LTRACEF("allocated 0x%lx\n", *paddrp);
    return 0;
}

static void free_page_table(void* vaddr, paddr_t paddr, uint page_size_shift) {
    DEBUG_ASSERT(page_size_shift <= MMU_MAX_PAGE_SIZE_SHIFT);

    LTRACEF("vaddr %p paddr 0x%lx page_size_shift %u\n", vaddr, paddr, page_size_shift);

    size_t size = 1U << page_size_shift;
    vm_page_t* page;

    if (size >= PAGE_SIZE) {
        page = paddr_to_vm_page(paddr);
        if (!page)
            panic("bad page table paddr 0x%lx\n", paddr);
        pmm_free_page(page);
    } else {
        free(vaddr);
    }
}

static volatile pte_t* arm64_mmu_get_page_table(vaddr_t index, uint page_size_shift,
                                                volatile pte_t* page_table) {
    pte_t pte;
    paddr_t paddr;
    void* vaddr;

    DEBUG_ASSERT(page_size_shift <= MMU_MAX_PAGE_SIZE_SHIFT);

    pte = page_table[index];
    switch (pte & MMU_PTE_DESCRIPTOR_MASK) {
    case MMU_PTE_DESCRIPTOR_INVALID: {
        status_t ret = alloc_page_table(&paddr, page_size_shift);
        if (ret) {
            TRACEF("failed to allocate page table\n");
            return NULL;
        }
        vaddr = paddr_to_kvaddr(paddr);

        LTRACEF("allocated page table, vaddr %p, paddr 0x%lx\n", vaddr, paddr);
        memset(vaddr, MMU_PTE_DESCRIPTOR_INVALID, 1U << page_size_shift);

        __asm__ volatile("dmb ishst" ::
                             : "memory");

        pte = paddr | MMU_PTE_L012_DESCRIPTOR_TABLE;
        page_table[index] = pte;
        LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n",
                page_table, index, pte);
        return static_cast<volatile pte_t*>(vaddr);
    }
    case MMU_PTE_L012_DESCRIPTOR_TABLE:
        paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
        LTRACEF("found page table %#" PRIxPTR "\n", paddr);
        return static_cast<volatile pte_t*>(paddr_to_kvaddr(paddr));

    case MMU_PTE_L012_DESCRIPTOR_BLOCK:
        return NULL;

    default:
        PANIC_UNIMPLEMENTED;
    }
}

static bool page_table_is_clear(volatile pte_t* page_table, uint page_size_shift) {
    int i;
    int count = 1U << (page_size_shift - 3);
    pte_t pte;

    for (i = 0; i < count; i++) {
        pte = page_table[i];
        if (pte != MMU_PTE_DESCRIPTOR_INVALID) {
            LTRACEF("page_table at %p still in use, index %d is %#" PRIx64 "\n",
                    page_table, i, pte);
            return false;
        }
    }

    LTRACEF("page table at %p is clear\n", page_table);
    return true;
}

static ssize_t arm64_mmu_unmap_pt(vaddr_t vaddr, vaddr_t vaddr_rel,
                                  size_t size,
                                  uint index_shift, uint page_size_shift,
                                  volatile pte_t* page_table, uint asid) {
    volatile pte_t* next_page_table;
    vaddr_t index;
    size_t chunk_size;
    vaddr_t vaddr_rem;
    vaddr_t block_size;
    vaddr_t block_mask;
    pte_t pte;
    paddr_t page_table_paddr;
    size_t unmap_size;

    LTRACEF("vaddr 0x%lx, vaddr_rel 0x%lx, size 0x%lx, index shift %u, page_size_shift %u, page_table %p\n",
            vaddr, vaddr_rel, size, index_shift, page_size_shift, page_table);

    unmap_size = 0;
    while (size) {
        block_size = 1UL << index_shift;
        block_mask = block_size - 1;
        vaddr_rem = vaddr_rel & block_mask;
        chunk_size = MIN(size, block_size - vaddr_rem);
        index = vaddr_rel >> index_shift;

        pte = page_table[index];

        if (index_shift > page_size_shift &&
            (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
            page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
            next_page_table = static_cast<volatile pte_t*>(paddr_to_kvaddr(page_table_paddr));
            arm64_mmu_unmap_pt(vaddr, vaddr_rem, chunk_size,
                               index_shift - (page_size_shift - 3),
                               page_size_shift,
                               next_page_table, asid);
            if (chunk_size == block_size ||
                page_table_is_clear(next_page_table, page_size_shift)) {
                LTRACEF("pte %p[0x%lx] = 0 (was page table)\n", page_table, index);
                page_table[index] = MMU_PTE_DESCRIPTOR_INVALID;
                __asm__ volatile("dmb ishst" ::
                                     : "memory");
                free_page_table(const_cast<pte_t*>(next_page_table), page_table_paddr,
                                page_size_shift);
            }
        } else if (pte) {
            LTRACEF("pte %p[0x%lx] = 0\n", page_table, index);
            page_table[index] = MMU_PTE_DESCRIPTOR_INVALID;
            CF;
            if (asid == MMU_ARM64_GLOBAL_ASID)
                ARM64_TLBI(vaae1is, vaddr >> 12);
            else
                ARM64_TLBI(vae1is, vaddr >> 12 | (vaddr_t)asid << 48);
        } else {
            LTRACEF("pte %p[0x%lx] already clear\n", page_table, index);
        }
        vaddr += chunk_size;
        vaddr_rel += chunk_size;
        size -= chunk_size;
        unmap_size += chunk_size;
    }

    return unmap_size;
}

static ssize_t arm64_mmu_map_pt(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                paddr_t paddr_in,
                                size_t size_in, pte_t attrs,
                                uint index_shift, uint page_size_shift,
                                volatile pte_t* page_table, uint asid) {
    ssize_t ret;
    volatile pte_t* next_page_table;
    vaddr_t index;
    vaddr_t vaddr = vaddr_in;
    vaddr_t vaddr_rel = vaddr_rel_in;
    paddr_t paddr = paddr_in;
    size_t size = size_in;
    size_t chunk_size;
    vaddr_t vaddr_rem;
    vaddr_t block_size;
    vaddr_t block_mask;
    pte_t pte;
    size_t mapped_size;

    LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", paddr %#" PRIxPTR
            ", size %#zx, attrs %#" PRIx64
            ", index shift %u, page_size_shift %u, page_table %p\n",
            vaddr, vaddr_rel, paddr, size, attrs,
            index_shift, page_size_shift, page_table);

    if ((vaddr_rel | paddr | size) & ((1UL << page_size_shift) - 1)) {
        TRACEF("not page aligned\n");
        return ERR_INVALID_ARGS;
    }

    mapped_size = 0;
    while (size) {
        block_size = 1UL << index_shift;
        block_mask = block_size - 1;
        vaddr_rem = vaddr_rel & block_mask;
        chunk_size = MIN(size, block_size - vaddr_rem);
        index = vaddr_rel >> index_shift;

        if (((vaddr_rel | paddr) & block_mask) ||
            (chunk_size != block_size) ||
            (index_shift > MMU_PTE_DESCRIPTOR_BLOCK_MAX_SHIFT)) {
            next_page_table = arm64_mmu_get_page_table(index, page_size_shift,
                                                       page_table);
            if (!next_page_table)
                goto err;

            ret = arm64_mmu_map_pt(vaddr, vaddr_rem, paddr, chunk_size, attrs,
                                   index_shift - (page_size_shift - 3),
                                   page_size_shift, next_page_table, asid);
            if (ret < 0)
                goto err;
        } else {
            pte = page_table[index];
            if (pte) {
                TRACEF("page table entry already in use, index %#" PRIxPTR ", %#" PRIx64 "\n",
                       index, pte);
                goto err;
            }

            pte = paddr | attrs;
            if (index_shift > page_size_shift)
                pte |= MMU_PTE_L012_DESCRIPTOR_BLOCK;
            else
                pte |= MMU_PTE_L3_DESCRIPTOR_PAGE;
            pte |= MMU_PTE_ATTR_NON_GLOBAL;
            LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n",
                    page_table, index, pte);
            page_table[index] = pte;
        }
        vaddr += chunk_size;
        vaddr_rel += chunk_size;
        paddr += chunk_size;
        size -= chunk_size;
        mapped_size += chunk_size;
    }

    return mapped_size;

err:
    arm64_mmu_unmap_pt(vaddr_in, vaddr_rel_in, size_in - size,
                       index_shift, page_size_shift, page_table, asid);
    DSB;
    return ERR_INTERNAL;
}

static int arm64_mmu_protect_pt(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                size_t size_in, pte_t attrs,
                                uint index_shift, uint page_size_shift,
                                volatile pte_t* page_table, uint asid) {
    int ret;
    volatile pte_t* next_page_table;
    vaddr_t index;
    vaddr_t vaddr = vaddr_in;
    vaddr_t vaddr_rel = vaddr_rel_in;
    size_t size = size_in;
    size_t chunk_size;
    vaddr_t vaddr_rem;
    vaddr_t block_size;
    vaddr_t block_mask;
    paddr_t page_table_paddr;
    pte_t pte;

    LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", size %#" PRIxPTR
            ", attrs %#" PRIx64
            ", index shift %u, page_size_shift %u, page_table %p\n",
            vaddr, vaddr_rel, size, attrs,
            index_shift, page_size_shift, page_table);

    if ((vaddr_rel | size) & ((1UL << page_size_shift) - 1)) {
        TRACEF("not page aligned\n");
        return ERR_INVALID_ARGS;
    }

    while (size) {
        block_size = 1UL << index_shift;
        block_mask = block_size - 1;
        vaddr_rem = vaddr_rel & block_mask;
        chunk_size = MIN(size, block_size - vaddr_rem);
        index = vaddr_rel >> index_shift;
        pte = page_table[index];

        if (index_shift > page_size_shift &&
            (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
            page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
            next_page_table = static_cast<volatile pte_t*>(paddr_to_kvaddr(page_table_paddr));
            ret = arm64_mmu_protect_pt(vaddr, vaddr_rem, chunk_size,
                                       attrs,
                                       index_shift - (page_size_shift - 3),
                                       page_size_shift,
                                       next_page_table, asid);
            if (ret != 0) {
                goto err;
            }
        } else if (pte) {
            pte = (pte & ~MMU_PTE_PERMISSION_MASK) | attrs;
            LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n",
                    page_table, index, pte);
            page_table[index] = pte;

            CF;
            if (asid == MMU_ARM64_GLOBAL_ASID) {
                ARM64_TLBI(vaae1is, vaddr >> 12);
            } else {
                ARM64_TLBI(vae1is, vaddr >> 12 | (vaddr_t)asid << 48);
            }
        } else {
            LTRACEF("page table entry does not exist, index %#" PRIxPTR
                    ", %#" PRIx64 "\n",
                    index, pte);
        }
        vaddr += chunk_size;
        vaddr_rel += chunk_size;
        size -= chunk_size;
    }

    DSB;
    return 0;

err:
    // TODO: Unroll any changes we've made, though in practice if we've reached
    // here there's a programming bug since the higher level region abstraction
    // should guard against us trying to change permissions on an umapped page
    DSB;
    return ERR_INTERNAL;
}

static ssize_t arm64_mmu_map(vaddr_t vaddr, paddr_t paddr, size_t size, pte_t attrs,
                             vaddr_t vaddr_base, uint top_size_shift,
                             uint top_index_shift, uint page_size_shift,
                             volatile pte_t* top_page_table, uint asid) {
    vaddr_t vaddr_rel = vaddr - vaddr_base;
    vaddr_t vaddr_rel_max = 1UL << top_size_shift;

    LTRACEF("vaddr %#" PRIxPTR ", paddr %#" PRIxPTR ", size %#" PRIxPTR
            ", attrs %#" PRIx64 ", asid %#x\n",
            vaddr, paddr, size, attrs, asid);

    if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
        TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR ", size %#" PRIxPTR "\n",
               vaddr, size, vaddr_base, vaddr_rel_max);
        return ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return ERR_INVALID_ARGS;
    }

    ssize_t ret = arm64_mmu_map_pt(vaddr, vaddr_rel, paddr, size, attrs,
                           top_index_shift, page_size_shift, top_page_table, asid);
    DSB;
    return ret;
}

static ssize_t arm64_mmu_unmap(vaddr_t vaddr, size_t size,
                               vaddr_t vaddr_base, uint top_size_shift,
                               uint top_index_shift, uint page_size_shift,
                               volatile pte_t* top_page_table, uint asid) {
    vaddr_t vaddr_rel = vaddr - vaddr_base;
    vaddr_t vaddr_rel_max = 1UL << top_size_shift;

    LTRACEF("vaddr 0x%lx, size 0x%lx, asid 0x%x\n", vaddr, size, asid);

    if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
        TRACEF("vaddr 0x%lx, size 0x%lx out of range vaddr 0x%lx, size 0x%lx\n",
               vaddr, size, vaddr_base, vaddr_rel_max);
        return ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return ERR_INVALID_ARGS;
    }

    ssize_t ret = arm64_mmu_unmap_pt(vaddr, vaddr_rel, size,
                       top_index_shift, page_size_shift, top_page_table, asid);
    DSB;
    return ret;
}

static status_t arm64_mmu_protect(vaddr_t vaddr, size_t size, pte_t attrs,
                             vaddr_t vaddr_base, uint top_size_shift,
                             uint top_index_shift, uint page_size_shift,
                             volatile pte_t* top_page_table, uint asid) {
    vaddr_t vaddr_rel = vaddr - vaddr_base;
    vaddr_t vaddr_rel_max = 1UL << top_size_shift;

    LTRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64
            ", asid %#x\n",
            vaddr, size, attrs, asid);

    if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
        TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR ", size %#" PRIxPTR "\n",
               vaddr, size, vaddr_base, vaddr_rel_max);
        return ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return ERR_INVALID_ARGS;
    }

    status_t ret = arm64_mmu_protect_pt(vaddr, vaddr_rel, size, attrs,
                           top_index_shift, page_size_shift, top_page_table, asid);
    DSB;
    return ret;
}

status_t arch_mmu_map(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t paddr, const size_t count, uint flags, size_t* mapped) {
    LTRACEF("vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %zu flags %#x\n",
            vaddr, paddr, count, flags);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
    DEBUG_ASSERT(aspace->tt_virt);

    DEBUG_ASSERT(is_valid_vaddr(aspace, vaddr));
    if (!is_valid_vaddr(aspace, vaddr))
        return ERR_OUT_OF_RANGE;

    if (!(flags & ARCH_MMU_FLAG_PERM_READ))
        return ERR_INVALID_ARGS;

    /* paddr and vaddr must be aligned */
    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr))
        return ERR_INVALID_ARGS;

    if (count == 0)
        return NO_ERROR;

    ssize_t ret;
    if (aspace->flags & ARCH_ASPACE_FLAG_KERNEL) {
        ret = arm64_mmu_map(vaddr, paddr, count * PAGE_SIZE,
                            mmu_flags_to_pte_attr(flags),
                            ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                            MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                            aspace->tt_virt, MMU_ARM64_GLOBAL_ASID);
    } else {
        ret = arm64_mmu_map(vaddr, paddr, count * PAGE_SIZE,
                            mmu_flags_to_pte_attr(flags),
                            0, MMU_USER_SIZE_SHIFT,
                            MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                            aspace->tt_virt, aspace->asid);
    }

    if (mapped) {
        *mapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
        DEBUG_ASSERT(*mapped <= count);
    }

    return (ret < 0) ? (status_t)ret : NO_ERROR;
}

status_t arch_mmu_unmap(arch_aspace_t* aspace, vaddr_t vaddr, const size_t count, size_t* unmapped) {
    LTRACEF("vaddr %#" PRIxPTR " count %zu\n", vaddr, count);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
    DEBUG_ASSERT(aspace->tt_virt);

    DEBUG_ASSERT(is_valid_vaddr(aspace, vaddr));

    if (!is_valid_vaddr(aspace, vaddr))
        return ERR_OUT_OF_RANGE;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    if (!IS_PAGE_ALIGNED(vaddr))
        return ERR_INVALID_ARGS;

    ssize_t ret;
    if (aspace->flags & ARCH_ASPACE_FLAG_KERNEL) {
        ret = arm64_mmu_unmap(vaddr, count * PAGE_SIZE,
                              ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                              MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                              aspace->tt_virt,
                              MMU_ARM64_GLOBAL_ASID);
    } else {
        ret = arm64_mmu_unmap(vaddr, count * PAGE_SIZE,
                              0, MMU_USER_SIZE_SHIFT,
                              MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                              aspace->tt_virt,
                              aspace->asid);
    }

    if (unmapped) {
        *unmapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
        DEBUG_ASSERT(*unmapped <= count);
    }

    return (ret < 0) ? (status_t)ret : 0;
}

status_t arch_mmu_protect(arch_aspace_t* aspace, vaddr_t vaddr, size_t count, uint flags) {
    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

    if (!is_valid_vaddr(aspace, vaddr))
        return ERR_INVALID_ARGS;

    if (!IS_PAGE_ALIGNED(vaddr))
        return ERR_INVALID_ARGS;

    if (!(flags & ARCH_MMU_FLAG_PERM_READ))
        return ERR_INVALID_ARGS;

    int ret;
    if (aspace->flags & ARCH_ASPACE_FLAG_KERNEL) {
        ret = arm64_mmu_protect(vaddr, count * PAGE_SIZE,
                                mmu_flags_to_pte_attr(flags),
                                ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                                MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                                aspace->tt_virt,
                                MMU_ARM64_GLOBAL_ASID);
    } else {
        ret = arm64_mmu_protect(vaddr, count * PAGE_SIZE,
                                mmu_flags_to_pte_attr(flags),
                                0, MMU_USER_SIZE_SHIFT,
                                MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                                aspace->tt_virt,
                                aspace->asid);
    }

    return ret;
}

status_t arch_mmu_init_aspace(arch_aspace_t* aspace, vaddr_t base, size_t size, uint flags) {
    LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, flags 0x%x\n",
            aspace, base, size, flags);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic != ARCH_ASPACE_MAGIC);

    /* validate that the base + size is sane and doesn't wrap */
    DEBUG_ASSERT(size > PAGE_SIZE);
    DEBUG_ASSERT(base + size - 1 > base);

    aspace->magic = ARCH_ASPACE_MAGIC;
    aspace->flags = flags;
    if (flags & ARCH_ASPACE_FLAG_KERNEL) {
        /* at the moment we can only deal with address spaces as globally defined */
        DEBUG_ASSERT(base == ~0UL << MMU_KERNEL_SIZE_SHIFT);
        DEBUG_ASSERT(size == 1UL << MMU_KERNEL_SIZE_SHIFT);

        aspace->base = base;
        aspace->size = size;
        aspace->tt_virt = arm64_kernel_translation_table;
        aspace->tt_phys = vaddr_to_paddr(const_cast<pte_t*>(aspace->tt_virt));
        aspace->asid = (uint16_t)MMU_ARM64_GLOBAL_ASID;
    } else {
        //DEBUG_ASSERT(base >= 0);
        DEBUG_ASSERT(base + size <= 1UL << MMU_USER_SIZE_SHIFT);

        if (arm64_mmu_alloc_asid(&aspace->asid) != NO_ERROR)
            return ERR_NO_MEMORY;

        aspace->base = base;
        aspace->size = size;

        paddr_t pa;
        volatile pte_t* va = static_cast<volatile pte_t*>(pmm_alloc_kpage(&pa, NULL));
        if (!va)
            return ERR_NO_MEMORY;

        aspace->tt_virt = va;
        aspace->tt_phys = pa;

        /* zero the top level translation table */
        /* XXX remove when PMM starts returning pre-zeroed pages */
        arch_zero_page(const_cast<pte_t*>(aspace->tt_virt));
    }

    LTRACEF("tt_phys %#" PRIxPTR " tt_virt %p\n",
            aspace->tt_phys, aspace->tt_virt);

    return NO_ERROR;
}

status_t arch_mmu_destroy_aspace(arch_aspace_t* aspace) {
    LTRACEF("aspace %p\n", aspace);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
    DEBUG_ASSERT((aspace->flags & ARCH_ASPACE_FLAG_KERNEL) == 0);

    // XXX make sure it's not mapped

    vm_page_t* page = paddr_to_vm_page(aspace->tt_phys);
    DEBUG_ASSERT(page);
    pmm_free_page(page);

    ARM64_TLBI(ASIDE1IS, aspace->asid);

    arm64_mmu_free_asid(aspace->asid);
    aspace->asid = 0;

    aspace->magic = 0;

    return NO_ERROR;
}

void arch_mmu_context_switch(arch_aspace_t* old_aspace, arch_aspace_t* aspace) {
    if (TRACE_CONTEXT_SWITCH)
        TRACEF("aspace %p\n", aspace);

    uint64_t tcr;
    uint64_t ttbr;
    if (aspace) {
        DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
        DEBUG_ASSERT((aspace->flags & ARCH_ASPACE_FLAG_KERNEL) == 0);

        tcr = MMU_TCR_FLAGS_USER;
        ttbr = ((uint64_t)aspace->asid << 48) | aspace->tt_phys;
        ARM64_WRITE_SYSREG(ttbr0_el1, ttbr);

        if (TRACE_CONTEXT_SWITCH)
            TRACEF("ttbr %#" PRIx64 ", tcr %#" PRIx64 "\n", ttbr, tcr);

    } else {
        tcr = MMU_TCR_FLAGS_KERNEL;

        if (TRACE_CONTEXT_SWITCH)
            TRACEF("tcr %#" PRIx64 "\n", tcr);
    }

    ARM64_WRITE_SYSREG(tcr_el1, tcr);
}

void arch_zero_page(void* _ptr) {
    uint8_t* ptr = (uint8_t*)_ptr;

    uint zva_size = 1u << arm64_zva_shift;

    uint8_t* end_ptr = ptr + PAGE_SIZE;
    do {
        __asm volatile("dc zva, %0" ::"r"(ptr));
        ptr += zva_size;
    } while (ptr != end_ptr);
}
