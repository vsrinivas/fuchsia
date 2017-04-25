// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <kernel/vm.h>
#include <platform/pc/bootloader.h>
#include <magenta/boot/multiboot.h>
#include <efi/boot-services.h>
#include <trace.h>

#include "platform_p.h"

#define LOCAL_TRACE 0

/* multiboot information passed in, if present */
extern multiboot_info_t *_multiboot_info;

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

        pmm_arena_info_t *arena = &mem_arenas[used];
        arena->base = base;
        arena->size = size;
        arena->name = "memory";
        arena->priority = 1;
        arena->flags = PMM_ARENA_FLAG_KMAP;
        used++;

        LTRACEF("Adding pmm range at %#" PRIxPTR " of %#zx bytes.\n", arena->base, arena->size);
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

    if (bootloader.e820_count) {
        seq->count = bootloader.e820_count;
        seq->map = bootloader.e820_table;
        range->reset(range);
        return 1;
    }

    return 0;
}


typedef struct efi_range_seq {
    void* base;
    size_t entrysz;
    int index;
    int count;
} efi_range_seq_t;

static void efi_range_reset(boot_addr_range_t *range)
{
    boot_addr_range_reset(range);

    efi_range_seq_t *seq = (efi_range_seq_t *)(range->seq);
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

static void efi_range_advance(boot_addr_range_t *range)
{
    efi_range_seq_t *seq = (efi_range_seq_t *)(range->seq);

    seq->index++;

    if (seq->index == seq->count) {
        /* reset range to signal that we're at the end of the map */
        efi_range_reset(range);
        return;
    }

    efi_memory_descriptor *entry = seq->base + (seq->index * seq->entrysz);
    efi_print("EFI: ", entry);
    range->base = entry->PhysicalStart;
    range->size = entry->NumberOfPages * PAGE_SIZE;
    range->is_reset = 0;
    range->is_mem = efi_is_mem(entry->Type);

    // coalesce adjacent memory ranges
    while ((seq->index + 1) < seq->count) {
        efi_memory_descriptor *next = seq->base + ((seq->index + 1) * seq->entrysz);
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

static int efi_range_init(boot_addr_range_t *range, efi_range_seq_t *seq)
{
    range->seq = seq;
    range->advance = &efi_range_advance;
    range->reset = &efi_range_reset;

    if (bootloader.efi_mmap &&
        (bootloader.efi_mmap_size > sizeof(uint64_t))) {
        seq->entrysz = *((uint64_t*) bootloader.efi_mmap);
        if (seq->entrysz < sizeof(efi_memory_descriptor)) {
            return 0;
        }

        seq->count = (bootloader.efi_mmap_size - sizeof(uint64_t)) / seq->entrysz;
        seq->base = bootloader.efi_mmap + sizeof(uint64_t);
        range->reset(range);
        return 1;
    } else {
        return 0;
    }
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

        // mem_lower is memory (KB) available at base=0
        // mem_upper is memory (KB) available at base=1MB
        range->base = 1024*1024;
        range->size = seq->info->mem_upper * 1024U;
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

    // Qemu does not set MB_INFO_MMAP flag, but does provide a
    // mmap table, which is necessary to see memory above 2GB.
    // Grub always sets both
    //
    // NOTE: We may misbehave under a bootloader that does not provide
    // a mmap table but does have a nonzero address and length.
    if ((seq->info->mmap_addr != 0) && (seq->info->mmap_length > 0)) {

        void* mmap_addr = (void*)X86_PHYS_TO_VIRT(seq->info->mmap_addr);

        /* we've been told the memory map is valid, so set it up */
        seq->mmap = (memory_map_t *)(uintptr_t)(mmap_addr);
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

static int platform_mem_range_init(void)
{
    boot_addr_range_t range;
    int count = 0;

    /* first try the efi memory table */
    efi_range_seq_t efi_seq;
    if (efi_range_init(&range, &efi_seq) &&
        (count = mem_arena_init(&range)))
        return count;

    /* then try getting range info from e820 */
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

static size_t cached_e820_entry_count;
static struct addr_range cached_e820_entries[64];

status_t enumerate_e820(enumerate_e820_callback callback, void* ctx) {
    if (callback == NULL)
        return ERR_INVALID_ARGS;

    if(!cached_e820_entry_count)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(cached_e820_entry_count <= countof(cached_e820_entries));
    for (size_t i = 0; i < cached_e820_entry_count; ++i)
        callback(cached_e820_entries[i].base, cached_e820_entries[i].size, ctx);

    return NO_ERROR;
}

/* Discover the basic memory map */
void platform_mem_init(void)
{
    int arena_count = platform_mem_range_init();
    for (int i = 0; i < arena_count; i++)
        pmm_add_arena(&mem_arenas[i]);

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

    cached_e820_entry_count = 0;
    if (efi_range_init(&range, &efi_seq) ||
        e820_range_init(&range, &e820_seq) ||
        multiboot_range_init(&range, &multiboot_seq)) {
        for (range.reset(&range),
             range.advance(&range);
             !range.is_reset; range.advance(&range)) {
            if (cached_e820_entry_count >= countof(cached_e820_entries)) {
                TRACEF("ERROR - Too many e820 entries to hold in the cache!\n");
                cached_e820_entry_count = 0;
                break;
            }

            struct addr_range* entry = &cached_e820_entries[cached_e820_entry_count++];
            entry->base = range.base;
            entry->size = range.size;

        }
    } else {
        TRACEF("ERROR - No e820 range entries found!  This is going to end badly for everyone.\n");
    }
}
