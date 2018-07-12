// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/bootstrap16.h>
#include <arch/x86/mmu.h>
#include <assert.h>
#include <efi/boot-services.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <inttypes.h>
#include <lib/memory_limit.h>
#include <platform.h>
#include <platform/pc/bootloader.h>
#include <string.h>
#include <trace.h>
#include <vm/vm.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/multiboot.h>
#include <zircon/types.h>

#include "platform_p.h"

#define LOCAL_TRACE 0

/* multiboot information passed in, if present */
extern multiboot_info_t* _multiboot_info;

struct addr_range {
    uint64_t base;
    uint64_t size;
    bool is_mem;
};

/* Values that will store the largest low-memory contiguous address space
 * that we can let the PCIe bus driver use for allocations */
paddr_t pcie_mem_lo_base;
size_t pcie_mem_lo_size;

#define DEFAULT_MEMEND (16 * 1024 * 1024)

/* boot_addr_range_t is an iterator which iterates over address ranges from
 * the boot loader
 */
struct boot_addr_range;

typedef void (*boot_addr_range_advance_func)(
    struct boot_addr_range* range_struct);
typedef void (*boot_addr_range_reset_func)(
    struct boot_addr_range* range_struct);

typedef struct boot_addr_range {
    /* the base of the current address range */
    uint64_t base;
    /* the size of the current address range */
    uint64_t size;
    /* whether this range contains memory */
    int is_mem;
    /* whether this range is currently reset and invalid */
    int is_reset;

    /* private information for the advance function to keep its place */
    void* seq;
    /* a function which advances this iterator to the next address range */
    boot_addr_range_advance_func advance;
    /* a function which resets this range and its sequencing information */
    boot_addr_range_reset_func reset;
} boot_addr_range_t;

/* a utility function to reset the common parts of a boot_addr_range_t */
static void boot_addr_range_reset(boot_addr_range_t* range) {
    range->base = 0;
    range->size = 0;
    range->is_mem = 0;
    range->is_reset = 1;
}

/* this function uses the boot_addr_range_t iterator to walk through address
 * ranges described by the boot loader. it fills in the mem_arenas global
 * array with the ranges of memory it finds, compacted to the start of the
 * array. it returns the total count of arenas which have been populated.
 */
static zx_status_t mem_arena_init(boot_addr_range_t* range) {
    mem_limit_ctx ctx;
    ctx.kernel_base = reinterpret_cast<uintptr_t>(__code_start);
    ctx.kernel_size = reinterpret_cast<uintptr_t>(_end) - ctx.kernel_base;
    ctx.ramdisk_base = reinterpret_cast<uintptr_t>(platform_get_ramdisk(&ctx.ramdisk_size));

    bool have_limit = (mem_limit_init(&ctx) == ZX_OK);

    // Set up a base arena template to use
    pmm_arena_info_t base_arena;
    snprintf(base_arena.name, sizeof(base_arena.name), "%s", "memory");
    base_arena.priority = 1;
    base_arena.flags = 0;

    for (range->reset(range), range->advance(range); !range->is_reset; range->advance(range)) {
        LTRACEF("Range at %#" PRIx64 " of %#" PRIx64 " bytes is %smemory.\n",
                range->base, range->size, range->is_mem ? "" : "not ");

        if (!range->is_mem)
            continue;

        /* trim off parts of memory ranges that are smaller than a page */
        uint64_t base = ROUNDUP(range->base, PAGE_SIZE);
        uint64_t size = ROUNDDOWN(range->base + range->size, PAGE_SIZE) -
                        base;

        /* trim any memory below 1MB for safety and SMP booting purposes */
        if (base < 1 * MB) {
            uint64_t adjust = 1 * MB - base;
            if (adjust >= size)
                continue;

            base += adjust;
            size -= adjust;
        }

        zx_status_t status = ZX_OK;
        if (have_limit) {
            status = mem_limit_add_arenas_from_range(&ctx, base, size, base_arena);
        }

        // If there is no limit, or we failed to add arenas from processing
        // ranges then add the original range.
        if (!have_limit || status != ZX_OK) {
            auto arena = base_arena;
            arena.base = base;
            arena.size = size;

            LTRACEF("Adding pmm range at %#" PRIxPTR " of %#zx bytes.\n", arena.base, arena.size);
            status = pmm_add_arena(&arena);

            // print a warning and continue
            if (status != ZX_OK) {
                printf("MEM: Failed to add pmm range at %#" PRIxPTR " size %#zx\n", arena.base, arena.size);
            }
        }
    }

    return ZX_OK;
}

typedef struct e820_range_seq {
    e820entry_t* map;
    int index;
    int count;
} e820_range_seq_t;

static void e820_range_reset(boot_addr_range_t* range) {
    boot_addr_range_reset(range);

    e820_range_seq_t* seq = (e820_range_seq_t*)(range->seq);
    seq->index = -1;
}

static void e820_range_advance(boot_addr_range_t* range) {
    e820_range_seq_t* seq = (e820_range_seq_t*)(range->seq);

    seq->index++;

    if (seq->index == seq->count) {
        /* reset range to signal that we're at the end of the map */
        e820_range_reset(range);
        return;
    }

    e820entry_t* entry = &seq->map[seq->index];
    range->base = entry->addr;
    range->size = entry->size;
    range->is_mem = (entry->type == E820_RAM) ? 1 : 0;
    range->is_reset = 0;
}

static zx_status_t e820_range_init(boot_addr_range_t* range, e820_range_seq_t* seq) {
    range->seq = seq;
    range->advance = &e820_range_advance;
    range->reset = &e820_range_reset;

    if (bootloader.e820_count) {
        seq->count = static_cast<int>(bootloader.e820_count);
        seq->map = static_cast<e820entry_t*>(bootloader.e820_table);
        range->reset(range);
        return ZX_OK;
    }

    return ZX_ERR_NO_MEMORY;
}

typedef struct efi_range_seq {
    void* base;
    size_t entrysz;
    int index;
    int count;
} efi_range_seq_t;

static void efi_range_reset(boot_addr_range_t* range) {
    boot_addr_range_reset(range);

    efi_range_seq_t* seq = (efi_range_seq_t*)(range->seq);
    seq->index = -1;
}

static int efi_is_mem(uint32_t type) {
    switch (type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
        return 1;
    default:
        return 0;
    }
}

static void efi_print(const char* tag, efi_memory_descriptor* e) {
    bool mb = e->NumberOfPages > 256;
    LTRACEF("%s%016lx %08x %lu%s\n",
            tag, e->PhysicalStart, e->Type,
            mb ? e->NumberOfPages / 256 : e->NumberOfPages * 4,
            mb ? "MB" : "KB");
}

static void efi_range_advance(boot_addr_range_t* range) {
    efi_range_seq_t* seq = (efi_range_seq_t*)(range->seq);

    seq->index++;

    if (seq->index == seq->count) {
        /* reset range to signal that we're at the end of the map */
        efi_range_reset(range);
        return;
    }

    const uintptr_t addr = reinterpret_cast<uintptr_t>(seq->base) + (seq->index * seq->entrysz);
    efi_memory_descriptor* entry = reinterpret_cast<efi_memory_descriptor*>(addr);
    efi_print("EFI: ", entry);
    range->base = entry->PhysicalStart;
    range->size = entry->NumberOfPages * PAGE_SIZE;
    range->is_reset = 0;
    range->is_mem = efi_is_mem(entry->Type);

    // coalesce adjacent memory ranges
    while ((seq->index + 1) < seq->count) {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(seq->base) +
                               ((seq->index + 1) * seq->entrysz);
        efi_memory_descriptor* next = reinterpret_cast<efi_memory_descriptor*>(addr);
        if ((range->base + range->size) != next->PhysicalStart) {
            break;
        }
        if (efi_is_mem(next->Type) != range->is_mem) {
            break;
        }
        efi_print("EFI+ ", next);
        range->size += next->NumberOfPages * PAGE_SIZE;
        seq->index++;
    }
}

static zx_status_t efi_range_init(boot_addr_range_t* range, efi_range_seq_t* seq) {
    range->seq = seq;
    range->advance = &efi_range_advance;
    range->reset = &efi_range_reset;

    if (bootloader.efi_mmap &&
        (bootloader.efi_mmap_size > sizeof(uint64_t))) {
        seq->entrysz = *((uint64_t*)bootloader.efi_mmap);
        if (seq->entrysz < sizeof(efi_memory_descriptor)) {
            return ZX_ERR_NO_MEMORY;
        }

        seq->count = static_cast<int>((bootloader.efi_mmap_size - sizeof(uint64_t)) / seq->entrysz);
        seq->base = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(bootloader.efi_mmap) + sizeof(uint64_t));
        range->reset(range);
        return ZX_OK;
    } else {
        return ZX_ERR_NO_MEMORY;
    }
}

typedef struct multiboot_range_seq {
    multiboot_info_t* info;
    memory_map_t* mmap;
    int index;
    int count;
} multiboot_range_seq_t;

static void multiboot_range_reset(boot_addr_range_t* range) {
    boot_addr_range_reset(range);

    multiboot_range_seq_t* seq = (multiboot_range_seq_t*)(range->seq);
    seq->index = -1;
}

static void multiboot_range_advance(boot_addr_range_t* range) {
    multiboot_range_seq_t* seq = (multiboot_range_seq_t*)(range->seq);

    if (seq->mmap) {
        /* memory map based range information */
        seq->index++;

        if (seq->index == seq->count) {
            multiboot_range_reset(range);
            return;
        }

        memory_map_t* entry = &seq->mmap[seq->index];

        range->base = entry->base_addr_high;
        range->base <<= 32;
        range->base |= entry->base_addr_low;

        range->size = entry->length_high;
        range->size <<= 32;
        range->size |= entry->length_low;

        range->is_mem = (entry->type == MB_MMAP_TYPE_AVAILABLE) ? 1 : 0;

        range->is_reset = 0;
    } else {
        /* scalar info about a single range */
        if (!range->is_reset) {
            /* toggle back and forth between reset and valid */
            multiboot_range_reset(range);
            return;
        }

        // mem_lower is memory (KB) available at base=0
        // mem_upper is memory (KB) available at base=1MB
        range->base = 1024 * 1024;
        range->size = seq->info->mem_upper * 1024U;
        range->is_mem = 1;
        range->is_reset = 0;
    }
}

static zx_status_t multiboot_range_init(boot_addr_range_t* range,
                                multiboot_range_seq_t* seq) {
    LTRACEF("_multiboot_info %p\n", _multiboot_info);

    range->seq = seq;
    range->advance = &multiboot_range_advance;
    range->reset = &multiboot_range_reset;

    if (_multiboot_info == NULL) {
        /* no multiboot info found. */
        return ZX_ERR_NO_MEMORY;
    }

    seq->info = (multiboot_info_t*)X86_PHYS_TO_VIRT(_multiboot_info);
    seq->mmap = NULL;
    seq->count = 0;

    // if the MMAP flag is set and the address/length seems sane, parse the multiboot
    // memory map table
    if ((seq->info->flags & MB_INFO_MMAP) && (seq->info->mmap_addr != 0) && (seq->info->mmap_length > 0)) {

        void* mmap_addr = (void*)X86_PHYS_TO_VIRT(seq->info->mmap_addr);

        /* we've been told the memory map is valid, so set it up */
        seq->mmap = (memory_map_t*)(uintptr_t)(mmap_addr);
        seq->count = static_cast<int>(seq->info->mmap_length / sizeof(memory_map_t));

        multiboot_range_reset(range);
        return ZX_OK;
    }

    if (seq->info->flags & MB_INFO_MEM_SIZE) {
        /* no additional setup required for the scalar range */
        return ZX_OK;
    }

    /* no memory information in the multiboot info */
    return ZX_ERR_NO_MEMORY;
}

static int addr_range_cmp(const void* p1, const void* p2) {
    const struct addr_range* a1 = static_cast<const struct addr_range*>(p1);
    const struct addr_range* a2 = static_cast<const struct addr_range*>(p2);

    if (a1->base < a2->base)
        return -1;
    else if (a1->base == a2->base)
        return 0;
    return 1;
}

static zx_status_t platform_mem_range_init(void) {
    zx_status_t status;
    boot_addr_range_t range;

    /* first try the efi memory table */
    efi_range_seq_t efi_seq;
    status = efi_range_init(&range, &efi_seq);
    if (status == ZX_OK) {
        status = mem_arena_init(&range);
        if (status != ZX_OK) {
            printf("MEM: failure while adding EFI memory ranges\n");
        }
        return ZX_OK;
    }

    /* then try getting range info from e820 */
    e820_range_seq_t e820_seq;
    status = e820_range_init(&range, &e820_seq);
    if (status == ZX_OK) {
        status = mem_arena_init(&range);
        if (status != ZX_OK) {
            printf("MEM: failure while adding e820 memory ranges\n");
        }
        return ZX_OK;
    }

    /* if no ranges were found, try multiboot */
    multiboot_range_seq_t multiboot_seq;
    status = multiboot_range_init(&range, &multiboot_seq);
    if (status == ZX_OK) {
        status = mem_arena_init(&range);
        if (status != ZX_OK) {
            printf("MEM: failure while adding multiboot memory ranges\n");
        }
        return ZX_OK;
    }

    /* if still no ranges were found, make a safe guess */
    printf("MEM: no arena range source: falling back to fixed size\n");
    e820_range_init(&range, &e820_seq);
    e820entry_t entry = {
        .addr = 0,
        .size = DEFAULT_MEMEND,
        .type = E820_RAM,
    };
    e820_seq.map = &entry;
    e820_seq.count = 1;
    return mem_arena_init(&range);
}

static size_t cached_e820_entry_count;
static struct addr_range cached_e820_entries[64];

zx_status_t enumerate_e820(enumerate_e820_callback callback, void* ctx) {
    if (callback == NULL)
        return ZX_ERR_INVALID_ARGS;

    if (!cached_e820_entry_count)
        return ZX_ERR_BAD_STATE;

    DEBUG_ASSERT(cached_e820_entry_count <= fbl::count_of(cached_e820_entries));
    for (size_t i = 0; i < cached_e820_entry_count; ++i)
        callback(cached_e820_entries[i].base, cached_e820_entries[i].size,
                 cached_e820_entries[i].is_mem, ctx);

    return ZX_OK;
}

/* Discover the basic memory map */
void pc_mem_init(void) {
    if (platform_mem_range_init() != ZX_OK) {
        TRACEF("Error adding arenas from provided memory tables.\n");
    }

    // Cache the e820 entries so that they will be available for enumeration
    // later in the boot.
    //
    // TODO(teisenbe, johngro): do not hardcode a limit on the number of
    // entries we may have.  Find some other way to make this information
    // available at any point in time after we boot.
    boot_addr_range_t range;
    efi_range_seq_t efi_seq;
    e820_range_seq_t e820_seq;
    multiboot_range_seq_t multiboot_seq;
    bool initialized_bootstrap16 = false;

    cached_e820_entry_count = 0;
    if ((efi_range_init(&range, &efi_seq) == ZX_OK) ||
        (e820_range_init(&range, &e820_seq) == ZX_OK) ||
        (multiboot_range_init(&range, &multiboot_seq) == ZX_OK)) {
        for (range.reset(&range),
             range.advance(&range);
             !range.is_reset; range.advance(&range)) {
            if (cached_e820_entry_count >= fbl::count_of(cached_e820_entries)) {
                TRACEF("ERROR - Too many e820 entries to hold in the cache!\n");
                cached_e820_entry_count = 0;
                break;
            }

            struct addr_range* entry = &cached_e820_entries[cached_e820_entry_count++];
            entry->base = range.base;
            entry->size = range.size;
            entry->is_mem = range.is_mem ? true : false;

            const uint64_t alloc_size = 2 * PAGE_SIZE;
            const uint64_t min_base = 2 * PAGE_SIZE;
            if (!initialized_bootstrap16 && entry->is_mem &&
                entry->base <= 1 * MB - alloc_size && entry->size >= alloc_size) {

                uint64_t adj_base = entry->base;
                if (entry->base < min_base) {
                    uint64_t size_adj = min_base - entry->base;
                    if (entry->size < size_adj + alloc_size) {
                        continue;
                    }
                    adj_base = min_base;
                }

                LTRACEF("Selected %" PRIxPTR " as bootstrap16 region\n", adj_base);
                x86_bootstrap16_init(adj_base);
                initialized_bootstrap16 = true;
            }
        }
    } else {
        TRACEF("ERROR - No e820 range entries found!  This is going to end badly for everyone.\n");
    }

    if (!initialized_bootstrap16) {
        TRACEF("WARNING - Failed to assign bootstrap16 region, SMP won't work\n");
    }
}
