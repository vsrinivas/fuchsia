// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <arch/aspace.h>
#include <arch/mmu.h>
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>
#include <lib/heap.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
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

// The main translation table.
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
            return MX_ERR_NO_MEMORY;
        }
    } while ((asid_pool[new_asid >> 6] & (1 << (new_asid % 64))) || (new_asid == 0));

    asid_pool[new_asid >> 6] = asid_pool[new_asid >> 6] | (1 << (new_asid % 64));

    mutex_release(&asid_lock);

    *asid = new_asid;

    return MX_OK;
}

static status_t arm64_mmu_free_asid(uint16_t asid) {

    mutex_acquire(&asid_lock);

    asid_pool[asid >> 6] = asid_pool[asid >> 6] & ~(1 << (asid % 64));

    mutex_release(&asid_lock);

    return MX_OK;
}

// Convert user level mmu flags to flags that go in L1 descriptors.
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
        // Invalid user-supplied flag.
        DEBUG_ASSERT(1);
        return MX_ERR_INVALID_ARGS;
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

status_t ArmArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
    ulong index;
    uint index_shift;
    uint page_size_shift;
    pte_t pte;
    pte_t pte_addr;
    uint descriptor_type;
    volatile pte_t* page_table;
    vaddr_t vaddr_rem;

    canary_.Assert();
    LTRACEF("aspace %p, vaddr 0x%lx\n", this, vaddr);

    DEBUG_ASSERT(tt_virt_);

    DEBUG_ASSERT(IsValidVaddr(vaddr));
    if (!IsValidVaddr(vaddr))
        return MX_ERR_OUT_OF_RANGE;

    // Compute shift values based on if this address space is for kernel or user space.
    if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
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

    page_table = tt_virt_;

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
            return MX_ERR_NOT_FOUND;

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
    if (mmu_flags) {
        *mmu_flags = 0;
        if (pte & MMU_PTE_ATTR_NON_SECURE)
            *mmu_flags |= ARCH_MMU_FLAG_NS;
        switch (pte & MMU_PTE_ATTR_ATTR_INDEX_MASK) {
        case MMU_PTE_ATTR_STRONGLY_ORDERED:
            *mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
            break;
        case MMU_PTE_ATTR_DEVICE:
            *mmu_flags |= ARCH_MMU_FLAG_UNCACHED_DEVICE;
            break;
        case MMU_PTE_ATTR_NORMAL_MEMORY:
            break;
        default:
            PANIC_UNIMPLEMENTED;
        }
        *mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
        switch (pte & MMU_PTE_ATTR_AP_MASK) {
        case MMU_PTE_ATTR_AP_P_RW_U_NA:
            *mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
            break;
        case MMU_PTE_ATTR_AP_P_RW_U_RW:
            *mmu_flags |= ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE;
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_NA:
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_RO:
            *mmu_flags |= ARCH_MMU_FLAG_PERM_USER;
            break;
        }
        if (!((pte & MMU_PTE_ATTR_UXN) && (pte & MMU_PTE_ATTR_PXN))) {
            *mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
        }
    }
    LTRACEF("va 0x%lx, paddr 0x%lx, flags 0x%x\n",
            vaddr, paddr ? *paddr : ~0UL, mmu_flags ? *mmu_flags : ~0U);
    return 0;
}

status_t ArmArchVmAspace::AllocPageTable(paddr_t* paddrp, uint page_size_shift) {
    size_t size = 1UL << page_size_shift;

    DEBUG_ASSERT(page_size_shift <= MMU_MAX_PAGE_SIZE_SHIFT);

    LTRACEF("page_size_shift %u\n", page_size_shift);

    if (size >= PAGE_SIZE) {
        size_t count = size / PAGE_SIZE;
        size_t ret = pmm_alloc_contiguous(count, PMM_ALLOC_FLAG_KMAP,
                                          static_cast<uint8_t>(page_size_shift), paddrp, NULL);
        if (ret != count)
            return MX_ERR_NO_MEMORY;

        pt_pages_ += count;
    } else if (size == PAGE_SIZE) {
        void* vaddr = pmm_alloc_kpage(paddrp, NULL);
        if (!vaddr)
            return MX_ERR_NO_MEMORY;

        pt_pages_++;
    } else {
        void* vaddr = memalign(size, size);
        if (!vaddr)
            return MX_ERR_NO_MEMORY;
        *paddrp = vaddr_to_paddr(vaddr);
        if (*paddrp == 0) {
            free(vaddr);
            return MX_ERR_NO_MEMORY;
        }
        pt_pages_++;
    }

    LTRACEF("allocated 0x%lx\n", *paddrp);
    return 0;
}

void ArmArchVmAspace::FreePageTable(void* vaddr, paddr_t paddr, uint page_size_shift) {
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
    pt_pages_--;
}

volatile pte_t* ArmArchVmAspace::GetPageTable(vaddr_t index, uint page_size_shift,
                                              volatile pte_t* page_table) {
    pte_t pte;
    paddr_t paddr;
    void* vaddr;

    DEBUG_ASSERT(page_size_shift <= MMU_MAX_PAGE_SIZE_SHIFT);

    pte = page_table[index];
    switch (pte & MMU_PTE_DESCRIPTOR_MASK) {
    case MMU_PTE_DESCRIPTOR_INVALID: {
        status_t ret = AllocPageTable(&paddr, page_size_shift);
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

ssize_t ArmArchVmAspace::UnmapPageTable(vaddr_t vaddr, vaddr_t vaddr_rel,
                                        size_t size, uint index_shift,
                                        uint page_size_shift,
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
            UnmapPageTable(vaddr, vaddr_rem, chunk_size,
                           index_shift - (page_size_shift - 3),
                           page_size_shift, next_page_table, asid);
            if (chunk_size == block_size ||
                page_table_is_clear(next_page_table, page_size_shift)) {
                LTRACEF("pte %p[0x%lx] = 0 (was page table)\n", page_table, index);
                page_table[index] = MMU_PTE_DESCRIPTOR_INVALID;
                __asm__ volatile("dmb ishst" ::
                                     : "memory");
                FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr,
                              page_size_shift);
            }
        } else if (pte) {
            LTRACEF("pte %p[0x%lx] = 0\n", page_table, index);
            page_table[index] = MMU_PTE_DESCRIPTOR_INVALID;
            fbl::atomic_signal_fence();
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

ssize_t ArmArchVmAspace::MapPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                      paddr_t paddr_in, size_t size_in,
                                      pte_t attrs, uint index_shift,
                                      uint page_size_shift,
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
        return MX_ERR_INVALID_ARGS;
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
            next_page_table = GetPageTable(index, page_size_shift, page_table);
            if (!next_page_table)
                goto err;

            ret = MapPageTable(vaddr, vaddr_rem, paddr, chunk_size, attrs,
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
    UnmapPageTable(vaddr_in, vaddr_rel_in, size_in - size, index_shift,
                   page_size_shift, page_table, asid);
    DSB;
    return MX_ERR_INTERNAL;
}

int ArmArchVmAspace::ProtectPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
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
        return MX_ERR_INVALID_ARGS;
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
            ret = ProtectPageTable(vaddr, vaddr_rem, chunk_size, attrs,
                                   index_shift - (page_size_shift - 3),
                                   page_size_shift, next_page_table, asid);
            if (ret != 0) {
                goto err;
            }
        } else if (pte) {
            pte = (pte & ~MMU_PTE_PERMISSION_MASK) | attrs;
            LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n",
                    page_table, index, pte);
            page_table[index] = pte;

            fbl::atomic_signal_fence();
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
    return MX_ERR_INTERNAL;
}

ssize_t ArmArchVmAspace::MapPages(vaddr_t vaddr, paddr_t paddr, size_t size,
                                  pte_t attrs, vaddr_t vaddr_base, uint top_size_shift,
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
        return MX_ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return MX_ERR_INVALID_ARGS;
    }

    ssize_t ret = MapPageTable(vaddr, vaddr_rel, paddr, size, attrs,
                               top_index_shift, page_size_shift, top_page_table,
                               asid);
    DSB;
    return ret;
}

ssize_t ArmArchVmAspace::UnmapPages(vaddr_t vaddr, size_t size,
                                    vaddr_t vaddr_base,
                                    uint top_size_shift,
                                    uint top_index_shift,
                                    uint page_size_shift,
                                    volatile pte_t* top_page_table,
                                    uint asid) {
    vaddr_t vaddr_rel = vaddr - vaddr_base;
    vaddr_t vaddr_rel_max = 1UL << top_size_shift;

    LTRACEF("vaddr 0x%lx, size 0x%lx, asid 0x%x\n", vaddr, size, asid);

    if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
        TRACEF("vaddr 0x%lx, size 0x%lx out of range vaddr 0x%lx, size 0x%lx\n",
               vaddr, size, vaddr_base, vaddr_rel_max);
        return MX_ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return MX_ERR_INVALID_ARGS;
    }

    ssize_t ret = UnmapPageTable(vaddr, vaddr_rel, size, top_index_shift,
                                 page_size_shift, top_page_table, asid);
    DSB;
    return ret;
}

status_t ArmArchVmAspace::ProtectPages(vaddr_t vaddr, size_t size, pte_t attrs,
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
        return MX_ERR_INVALID_ARGS;
    }

    if (!top_page_table) {
        TRACEF("page table is NULL\n");
        return MX_ERR_INVALID_ARGS;
    }

    status_t ret = ProtectPageTable(vaddr, vaddr_rel, size, attrs,
                                    top_index_shift, page_size_shift,
                                    top_page_table, asid);
    DSB;
    return ret;
}

status_t ArmArchVmAspace::Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                              uint mmu_flags, size_t* mapped) {
    canary_.Assert();
    LTRACEF("vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %zu flags %#x\n",
            vaddr, paddr, count, mmu_flags);

    DEBUG_ASSERT(tt_virt_);

    DEBUG_ASSERT(IsValidVaddr(vaddr));
    if (!IsValidVaddr(vaddr))
        return MX_ERR_OUT_OF_RANGE;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return MX_ERR_INVALID_ARGS;

    // paddr and vaddr must be aligned.
    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr))
        return MX_ERR_INVALID_ARGS;

    if (count == 0)
        return MX_OK;

    ssize_t ret;
    {
        fbl::AutoLock a(&lock_);

        if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
            ret = MapPages(vaddr, paddr, count * PAGE_SIZE,
                           mmu_flags_to_pte_attr(mmu_flags),
                           ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                           MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                           tt_virt_, MMU_ARM64_GLOBAL_ASID);
        } else {
            ret = MapPages(vaddr, paddr, count * PAGE_SIZE,
                           mmu_flags_to_pte_attr(mmu_flags),
                           0, MMU_USER_SIZE_SHIFT,
                           MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                           tt_virt_, asid_);
        }
    }

    if (mapped) {
        *mapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
        DEBUG_ASSERT(*mapped <= count);
    }

    return (ret < 0) ? (status_t)ret : MX_OK;
}

status_t ArmArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
    canary_.Assert();
    LTRACEF("vaddr %#" PRIxPTR " count %zu\n", vaddr, count);

    DEBUG_ASSERT(tt_virt_);

    DEBUG_ASSERT(IsValidVaddr(vaddr));

    if (!IsValidVaddr(vaddr))
        return MX_ERR_OUT_OF_RANGE;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
    if (!IS_PAGE_ALIGNED(vaddr))
        return MX_ERR_INVALID_ARGS;

    fbl::AutoLock a(&lock_);

    ssize_t ret;
    if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
        ret = UnmapPages(vaddr, count * PAGE_SIZE,
                         ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                         MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                         tt_virt_,
                         MMU_ARM64_GLOBAL_ASID);
    } else {
        ret = UnmapPages(vaddr, count * PAGE_SIZE,
                         0, MMU_USER_SIZE_SHIFT,
                         MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                         tt_virt_,
                         asid_);
    }

    if (unmapped) {
        *unmapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
        DEBUG_ASSERT(*unmapped <= count);
    }

    return (ret < 0) ? (status_t)ret : 0;
}

status_t ArmArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
    canary_.Assert();

    if (!IsValidVaddr(vaddr))
        return MX_ERR_INVALID_ARGS;

    if (!IS_PAGE_ALIGNED(vaddr))
        return MX_ERR_INVALID_ARGS;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return MX_ERR_INVALID_ARGS;

    fbl::AutoLock a(&lock_);

    int ret;
    if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
        ret = ProtectPages(vaddr, count * PAGE_SIZE,
                           mmu_flags_to_pte_attr(mmu_flags),
                           ~0UL << MMU_KERNEL_SIZE_SHIFT, MMU_KERNEL_SIZE_SHIFT,
                           MMU_KERNEL_TOP_SHIFT, MMU_KERNEL_PAGE_SIZE_SHIFT,
                           tt_virt_, MMU_ARM64_GLOBAL_ASID);
    } else {
        ret = ProtectPages(vaddr, count * PAGE_SIZE,
                           mmu_flags_to_pte_attr(mmu_flags),
                           0, MMU_USER_SIZE_SHIFT,
                           MMU_USER_TOP_SHIFT, MMU_USER_PAGE_SIZE_SHIFT,
                           tt_virt_, asid_);
    }

    return ret;
}

status_t ArmArchVmAspace::Init(vaddr_t base, size_t size, uint flags) {
    canary_.Assert();
    LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, flags 0x%x\n",
            this, base, size, flags);

    fbl::AutoLock a(&lock_);

    // Validate that the base + size is sane and doesn't wrap.
    DEBUG_ASSERT(size > PAGE_SIZE);
    DEBUG_ASSERT(base + size - 1 > base);

    flags_ = flags;
    if (flags & ARCH_ASPACE_FLAG_KERNEL) {
        // At the moment we can only deal with address spaces as globally defined.
        DEBUG_ASSERT(base == ~0UL << MMU_KERNEL_SIZE_SHIFT);
        DEBUG_ASSERT(size == 1UL << MMU_KERNEL_SIZE_SHIFT);

        base_ = base;
        size_ = size;
        tt_virt_ = arm64_kernel_translation_table;
        tt_phys_ = vaddr_to_paddr(const_cast<pte_t*>(tt_virt_));
        asid_ = (uint16_t)MMU_ARM64_GLOBAL_ASID;
    } else {
        //DEBUG_ASSERT(base >= 0);
        DEBUG_ASSERT(base + size <= 1UL << MMU_USER_SIZE_SHIFT);

        if (arm64_mmu_alloc_asid(&asid_) != MX_OK)
            return MX_ERR_NO_MEMORY;

        base_ = base;
        size_ = size;

        paddr_t pa;
        volatile pte_t* va = static_cast<volatile pte_t*>(pmm_alloc_kpage(&pa, NULL));
        if (!va)
            return MX_ERR_NO_MEMORY;

        tt_virt_ = va;
        tt_phys_ = pa;

        // zero the top level translation table.
        // XXX remove when PMM starts returning pre-zeroed pages.
        arch_zero_page(const_cast<pte_t*>(tt_virt_));
    }
    pt_pages_ = 1;

    LTRACEF("tt_phys %#" PRIxPTR " tt_virt %p\n", tt_phys_, tt_virt_);

    return MX_OK;
}

status_t ArmArchVmAspace::Destroy() {
    canary_.Assert();
    LTRACEF("aspace %p\n", this);

    fbl::AutoLock a(&lock_);

    DEBUG_ASSERT((flags_ & ARCH_ASPACE_FLAG_KERNEL) == 0);

    // XXX make sure it's not mapped

    vm_page_t* page = paddr_to_vm_page(tt_phys_);
    DEBUG_ASSERT(page);
    pmm_free_page(page);

    ARM64_TLBI(ASIDE1IS, asid_);

    arm64_mmu_free_asid(asid_);
    asid_ = 0;

    return MX_OK;
}

void ArmArchVmAspace::ContextSwitch(ArmArchVmAspace* old_aspace, ArmArchVmAspace* aspace) {
    if (TRACE_CONTEXT_SWITCH)
        TRACEF("aspace %p\n", aspace);

    uint64_t tcr;
    uint64_t ttbr;
    if (aspace) {
        aspace->canary_.Assert();
        DEBUG_ASSERT((aspace->flags_ & ARCH_ASPACE_FLAG_KERNEL) == 0);

        tcr = MMU_TCR_FLAGS_USER;
        ttbr = ((uint64_t)aspace->asid_ << 48) | aspace->tt_phys_;
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

ArmArchVmAspace::ArmArchVmAspace() {}

ArmArchVmAspace::~ArmArchVmAspace() {
    // TODO: check that we've destroyed the aspace
}

vaddr_t ArmArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                                  vaddr_t end, uint next_region_mmu_flags,
                                  vaddr_t align, size_t size, uint mmu_flags) {
    canary_.Assert();
    return PAGE_ALIGN(base);
}
