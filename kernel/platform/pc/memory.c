// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <trace.h>
#include <kernel/vm.h>
#include "platform_p.h"
#include <platform/multiboot.h>

#define LOCAL_TRACE 0

/* multiboot information passed in, if present */
extern multiboot_info_t *_multiboot_info;
extern void *_zero_page_boot_params;

/* statically allocate an array of pmm_arena_info_ts to be filled in at boot time */
#define PMM_ARENAS 16
static pmm_arena_info_t mem_arenas[PMM_ARENAS];

struct addr_range {
    uint64_t base;
    uint64_t size;
};

/* Values that will store the largest low-memory contiguous address space
 * that we can let the PCIe bus driver use for allocations */
paddr_t pcie_mem_lo_base;
size_t pcie_mem_lo_size;

/* Store the PIO region for PCIe; make thes variables so we can easily
 * change them later */
#define BASE_PMIO_ADDR 0x8000
uint16_t pcie_pio_base = BASE_PMIO_ADDR;
uint16_t pcie_pio_size = 0x10000 - BASE_PMIO_ADDR;

/* Scratch space for storing discovered address ranges so we can sort them */
#define MAX_ADDRESS_RANGES 32
static struct addr_range address_ranges[MAX_ADDRESS_RANGES];

#define DEFAULT_MEMEND (16*1024*1024)

/* boot_addr_range_t is an iterator which iterates over address ranges from
 * the boot loader
 */
struct boot_addr_range;

typedef void (*boot_addr_range_advance_func)(
        struct boot_addr_range *range_struct);
typedef void (*boot_addr_range_reset_func)(
        struct boot_addr_range *range_struct);

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
    void *seq;
    /* a function which advances this iterator to the next address range */
    boot_addr_range_advance_func advance;
    /* a function which resets this range and its sequencing information */
    boot_addr_range_reset_func reset;
} boot_addr_range_t;

/* a utility function to reset the common parts of a boot_addr_range_t */
static void boot_addr_range_reset(boot_addr_range_t *range)
{
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
static int mem_arena_init(boot_addr_range_t *range)
{
    int used = 0;

    for (range->reset(range), range->advance(range);
         !range->is_reset && used < PMM_ARENAS;
         range->advance(range)) {

        LTRACEF("Range at %#" PRIx64 " of %#" PRIx64 " bytes is %smemory.\n",
                range->base, range->size, range->is_mem ? "" : "not ");

        if (!range->is_mem)
            continue;

        /* trim off parts of memory ranges that are smaller than a page */
        uint64_t base = ROUNDUP(range->base, PAGE_SIZE);
        uint64_t size = ROUNDDOWN(range->base + range->size, PAGE_SIZE) -
                base;

        /* trim any memory below 1MB for safety and SMP booting purposes */
        if (base < 1*MB) {
            uint64_t adjust = 1*MB - base;
            if (adjust >= size)
                continue;

            base += adjust;
            size -= adjust;
        }

#if ARCH_X86_32
        /* X86-32 can only handle up to 1GB of physical memory */
        if (base > 1*GB)
            continue;

        if (base + size > 1*GB) {
            uint64_t adjust = 1*GB - base;

            size -= adjust;
        }
#endif

        while (size && used < PMM_ARENAS) {
            pmm_arena_info_t *arena = &mem_arenas[used];

            arena->base = base;
            arena->size = size;

            if ((uint64_t)arena->base != base) {
                LTRACEF("Range base %#" PRIx64 " is too high.\n", base);
                break;
            }
            if ((uint64_t)arena->size != size) {
                LTRACEF("Range size %#" PRIx64 " is too large, splitting it.\n",
                        size);
                arena->size = -PAGE_SIZE;
            }

            size -= arena->size;
            base += arena->size;

            LTRACEF("Adding pmm range at %#" PRIxPTR " of %#zx bytes.\n",
                    arena->base, arena->size);

            arena->name = "memory";
            arena->priority = 1;
            arena->flags = PMM_ARENA_FLAG_KMAP;

            used++;
        }
    }

    return used;
}

#define E820_ENTRIES_OFFSET 0x1e8
#define E820_MAP_OFFSET 0x2d0

#define E820_RAM 1
#define E820_RESERVED 2
#define E820_ACPI 3
#define E820_NVS 4
#define E820_UNUSABLE 5

struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed));

typedef struct e820_range_seq {
    struct e820entry *map;
    int index;
    int count;
} e820_range_seq_t;

static void e820_range_reset(boot_addr_range_t *range)
{
    boot_addr_range_reset(range);

    e820_range_seq_t *seq = (e820_range_seq_t *)(range->seq);
    seq->index = -1;
}

static void e820_range_advance(boot_addr_range_t *range)
{
    e820_range_seq_t *seq = (e820_range_seq_t *)(range->seq);

    seq->index++;

    if (seq->index == seq->count) {
        /* reset range to signal that we're at the end of the map */
        e820_range_reset(range);
        return;
    }

    struct e820entry *entry = &seq->map[seq->index];
    range->base = entry->addr;
    range->size = entry->size;
    range->is_mem = (entry->type == E820_RAM) ? 1 : 0;
    range->is_reset = 0;
}

static int e820_range_init(boot_addr_range_t *range, e820_range_seq_t *seq)
{
    range->seq = seq;
    range->advance = &e820_range_advance;
    range->reset = &e820_range_reset;

    if (_zero_page_boot_params == NULL) {
        LTRACEF("No zero page found.\n");
        return 0;
    }

    uintptr_t zero_page = (uintptr_t)_zero_page_boot_params + KERNEL_BASE;

    seq->count = *(uint8_t *)(zero_page + E820_ENTRIES_OFFSET);
    LTRACEF("There are %d e820 mappings.\n", seq->count);

    seq->map = (void *)(zero_page + E820_MAP_OFFSET);

    range->reset(range);

    return 1;
}


typedef struct multiboot_range_seq {
    multiboot_info_t *info;
    memory_map_t *mmap;
    int index;
    int count;
} multiboot_range_seq_t;

static void multiboot_range_reset(boot_addr_range_t *range)
{
    boot_addr_range_reset(range);

    multiboot_range_seq_t *seq = (multiboot_range_seq_t *)(range->seq);
    seq->index = -1;
}

static void multiboot_range_advance(boot_addr_range_t *range)
{
    multiboot_range_seq_t *seq = (multiboot_range_seq_t *)(range->seq);

    if (seq->mmap) {
        /* memory map based range information */
        seq->index++;

        if (seq->index == seq->count) {
            multiboot_range_reset(range);
            return;
        }

        memory_map_t *entry = &seq->mmap[seq->index];

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

        range->base = seq->info->mem_lower * 1024U;
        range->size = (seq->info->mem_upper - seq->info->mem_lower) * 1024U;
        range->is_mem = 1;
        range->is_reset = 0;
    }
}

static int multiboot_range_init(boot_addr_range_t *range,
                                multiboot_range_seq_t *seq)
{
    LTRACEF("_multiboot_info %p\n", _multiboot_info);

    range->seq = seq;
    range->advance = &multiboot_range_advance;
    range->reset = &multiboot_range_reset;

    if (_multiboot_info == NULL) {
        /* no multiboot info found. */
        return 0;
    }

    seq->info = (multiboot_info_t *)X86_PHYS_TO_VIRT(_multiboot_info);
    seq->mmap = NULL;
    seq->count = 0;

    if (seq->info->flags & MB_INFO_MMAP) {
        /* we've been told the memory map is valid, so set it up */
        seq->mmap = (memory_map_t *)(uintptr_t)(seq->info->mmap_addr - 4);
        seq->count = seq->info->mmap_length / sizeof(memory_map_t);

        multiboot_range_reset(range);
        return 1;
    }

    if (seq->info->flags & MB_INFO_MEM_SIZE) {
        /* no additional setup required for the scalar range */
        return 1;
    }

    /* no memory information in the multiboot info */
    return 0;
}

static int addr_range_cmp(const void* p1, const void* p2)
{
    const struct addr_range *a1 = p1;
    const struct addr_range *a2 = p2;

    if (a1->base < a2->base)
        return -1;
    else if (a1->base == a2->base)
        return 0;
    return 1;
}

/* Find the largest low-memory gap in the memory map provided by the
 * bootloader to assign to PCIe.
 */
static void find_pcie_mmio_region(void)
{
    boot_addr_range_t range;

    e820_range_seq_t e820_seq;
    multiboot_range_seq_t multiboot_seq;
    if (!e820_range_init(&range, &e820_seq)) {
        if (!multiboot_range_init(&range, &multiboot_seq)) {
            pcie_mem_lo_base = 0;
            pcie_mem_lo_size = 0;
        }
    }

    uint num_ranges = 0;
    for (range.reset(&range), range.advance(&range);
         !range.is_reset;
         range.advance(&range)) {

        /* TODO(teisenbe): We can probably just dynamically allocate an array
         * here to avoid the too many address ranges case */
        if (num_ranges == MAX_ADDRESS_RANGES) {
            printf("WARNING: Too many address ranges, cannot allocate PCIe region\n");
            pcie_mem_lo_base = 0;
            pcie_mem_lo_size = 0;
            return;
        }
        address_ranges[num_ranges].base = range.base;
        address_ranges[num_ranges].size = range.size;
        /* make sure we don't wrap the address space */
        DEBUG_ASSERT(range.base <= range.base + range.size);
        num_ranges++;
    }
    if (num_ranges == 0) {
        return;
    }
    qsort(address_ranges, num_ranges, sizeof(address_ranges[0]), addr_range_cmp);

    /* Assume the ranges are non-overlapping and search for the biggest gap */
    for (uint i = 0; i < num_ranges - 1; ++i) {
        DEBUG_ASSERT(address_ranges[i].base < address_ranges[i + 1].base);
        uint64_t end = address_ranges[i].base + address_ranges[i].size;
        if (end > HIGH_ADDRESS_LIMIT) {
            break;
        }
        uint64_t next_start = address_ranges[i + 1].base;
        if (next_start > HIGH_ADDRESS_LIMIT) {
            next_start = HIGH_ADDRESS_LIMIT;
        }
        DEBUG_ASSERT(next_start >= end);
        uint64_t size = next_start - end;
        if (size > pcie_mem_lo_size) {
            pcie_mem_lo_size = size;
            pcie_mem_lo_base = end;
        }
    }

    uint64_t end = address_ranges[num_ranges - 1].base + address_ranges[num_ranges - 1].size;
    if (end < HIGH_ADDRESS_LIMIT) {
        uint64_t size = HIGH_ADDRESS_LIMIT - end;
        if (size > pcie_mem_lo_size) {
            pcie_mem_lo_size = size;
            pcie_mem_lo_base = end;
        }
    }
}

static int platform_mem_range_init(void)
{
    boot_addr_range_t range;
    int count = 0;

    /* try getting range info from e820 first */
    e820_range_seq_t e820_seq;
    if (e820_range_init(&range, &e820_seq) &&
        (count = mem_arena_init(&range)))
        return count;

    /* if no ranges were found, try multiboot */
    multiboot_range_seq_t multiboot_seq;
    if (multiboot_range_init(&range, &multiboot_seq) &&
        (count = mem_arena_init(&range)))
        return count;

    /* if still no ranges were found, make a safe guess */
    mem_arenas[0].name = "memory";
    mem_arenas[0].base = MEMBASE;
    mem_arenas[0].size = DEFAULT_MEMEND;
    mem_arenas[0].priority = 1;
    mem_arenas[0].flags = PMM_ARENA_FLAG_KMAP;
    return 1;
}

/* Discover the basic memory map */
void platform_mem_init(void)
{
    int arena_count = platform_mem_range_init();
    for (int i = 0; i < arena_count; i++)
        pmm_add_arena(&mem_arenas[i]);

    find_pcie_mmio_region();
}
