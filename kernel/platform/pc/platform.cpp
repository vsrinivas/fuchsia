// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <trace.h>
#include <arch/ops.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/mmu.h>
#include <dev/pcie_bus_driver.h>
#include <dev/uart.h>
#include <platform.h>
#include <mexec.h>
#include "platform_p.h"
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/pc/bootloader.h>
#include <platform/pc/memory.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <magenta/boot/bootdata.h>
#include <magenta/boot/multiboot.h>
#include <arch/mmu.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <vm/initial_map.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

#include <lib/cksum.h>

extern "C" {
#include <efi/runtime-services.h>
#include <efi/system-table.h>
};

#define LOCAL_TRACE 0

// Set to 1 to do crc32 checks on boot data
// KERNEL_LL_DEBUG and a uart or early gfxconsole are necessary
// to see the output from this debug feature
#define DEBUG_BOOT_DATA 0

extern multiboot_info_t* _multiboot_info;
extern bootdata_t* _bootdata_base;

pc_bootloader_info_t bootloader;

struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 64GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_ASPACE_BASE,
        .size = 64ULL*GB, /* x86-64 maps first 64GB by default */
        .flags = 0,
        .name = "memory"
    },
    /* KERNEL_SIZE of memory mapped where the kernel lives.
     * On x86-64, this only sticks around until the VM is brought up, after
     * that this will be replaced with mappings of the correct privileges. */
    {
        .phys = MEMBASE,
        .virt = KERNEL_BASE,
        .size = KERNEL_SIZE, /* x86 maps first KERNEL_SIZE by default */
        .flags = MMU_INITIAL_MAPPING_TEMPORARY,
        .name = "kernel_temp"
    },
    /* null entry to terminate the list */
    {}
};

static bool early_console_disabled;

static int process_bootitem(bootdata_t* bd, void* item) {
    switch (bd->type) {
    case BOOTDATA_ACPI_RSDP:
        if (bd->length >= sizeof(uint64_t)) {
            bootloader.acpi_rsdp = *((uint64_t*)item);
        }
        break;
    case BOOTDATA_EFI_SYSTEM_TABLE:
        if (bd->length >= sizeof(uint64_t)) {
            bootloader.efi_system_table = (void*) *((uint64_t*)item);
        }
        break;
    case BOOTDATA_FRAMEBUFFER: {
        if (bd->length >= sizeof(bootdata_swfb_t)) {
            memcpy(&bootloader.fb, item, sizeof(bootdata_swfb_t));
        }
        break;
    }
    case BOOTDATA_CMDLINE:
        if (bd->length > 0) {
            ((char*) item)[bd->length - 1] = 0;
            cmdline_append((char*) item);
        }
        break;
    case BOOTDATA_EFI_MEMORY_MAP:
        bootloader.efi_mmap = item;
        bootloader.efi_mmap_size = bd->length;
        break;
    case BOOTDATA_E820_TABLE:
        bootloader.e820_table = item;
        bootloader.e820_count = bd->length / sizeof(e820entry_t);
        break;
    case BOOTDATA_LASTLOG_NVRAM2:
        // fallthrough: this is a legacy/typo variant
    case BOOTDATA_LASTLOG_NVRAM:
        if (bd->length >= sizeof(bootdata_nvram_t)) {
            memcpy(&bootloader.nvram, item, sizeof(bootdata_nvram_t));
        }
        break;
    case BOOTDATA_DEBUG_UART:
        if (bd->length >= sizeof(bootdata_uart_t)) {
            memcpy(&bootloader.uart, item, sizeof(bootdata_uart_t));
        }
        break;
    case BOOTDATA_IGNORE:
        break;
    }
    return 0;
}

extern "C" void *boot_alloc_mem(size_t len);
extern "C" void boot_alloc_reserve(uintptr_t phys, size_t _len);

static void process_bootdata(bootdata_t* hdr, uintptr_t phys, bool verify) {
    if ((hdr->type != BOOTDATA_CONTAINER) ||
        (hdr->extra != BOOTDATA_MAGIC)) {
        printf("bootdata: invalid %08x %08x %08x %08x\n",
               hdr->type, hdr->length, hdr->extra, hdr->flags);
        return;
    }

    size_t hsz = sizeof(bootdata_t);
    if (hdr->flags & BOOTDATA_FLAG_EXTRA) {
        hsz += sizeof(bootextra_t);
    }

    size_t total_len = hdr->length + hsz;

    printf("bootdata: @ %p (%zu bytes)\n", hdr, total_len);

    bootdata_t* bd = reinterpret_cast<bootdata_t*>(reinterpret_cast<char*>(hdr) + hsz);

    size_t remain = hdr->length;
    while (remain > sizeof(bootdata_t)) {
        remain -= sizeof(bootdata_t);
        uintptr_t item = reinterpret_cast<uintptr_t>(bd + 1);

#if DEBUG_BOOT_DATA
        char tag[5];
        uint8_t* x = reinterpret_cast<uint8_t*>(&bd->type);
        unsigned n;
        for (n = 0; n < 4; n++) {
            tag[n] = ((*x >= ' ') && (*x <= 127)) ? *x : '.';
            x++;
        }
        tag[n] = 0;
        printf("bootdata: @ %p typ=%08x (%s) len=%08x ext=%08x flg=%08x\n",
               bd, bd->type, tag, bd->length, bd->extra, bd->flags);
#endif

        // check for extra header and process if it exists
        if (bd->flags & BOOTDATA_FLAG_EXTRA) {
            if (remain < sizeof(bootextra_t)) {
                printf("bootdata: truncated header\n");
                break;
            }
            bootextra_t* extra = reinterpret_cast<bootextra_t*>(item);
            item += sizeof(bootextra_t);
            remain -= sizeof(bootextra_t);
            if (extra->magic != BOOTITEM_MAGIC) {
                printf("bootdata: bad magic\n");
                break;
            }
        }

        size_t advance = BOOTDATA_ALIGN(bd->length);
        if (advance > remain) {
            printf("bootdata: truncated\n");
            break;
        }
#if DEBUG_BOOT_DATA
        if (verify && (bd->flags & BOOTDATA_FLAG_CRC32)) {
            if (!(bd->flags & BOOTDATA_FLAG_EXTRA)) {
                printf("bootdata: crc flag set but no extra data!\n");
                break;
            }
            bootextra_t* extra = reinterpret_cast<bootextra_t*>(item - sizeof(bootextra_t));
            uint32_t crc = 0;
            uint32_t tmp = extra->crc32;
            extra->crc32 = 0;
            crc = crc32(crc, reinterpret_cast<uint8_t*>(bd), sizeof(bootdata_t));
            crc = crc32(crc, reinterpret_cast<uint8_t*>(extra), sizeof(bootextra_t));
            crc = crc32(crc, reinterpret_cast<uint8_t*>(item), bd->length);
            extra->crc32 = tmp;
            printf("bootdata: crc %08x, computed %08x: %s\n", tmp, crc,
                   (tmp == crc) ? "OKAY" : "FAIL");
        }
#endif
        if (!verify) {
            if (process_bootitem(bd, reinterpret_cast<void*>(item))) {
                break;
            }
        }
        bd = reinterpret_cast<bootdata_t*>(item + advance);
        remain -= advance;
    }

    if (!verify) {
        boot_alloc_reserve(phys, total_len);
        bootloader.ramdisk_base = phys;
        bootloader.ramdisk_size = total_len;
    }
}

extern bool halt_on_panic;

static void platform_save_bootloader_data(bool verify) {
    if (_multiboot_info != NULL) {
        multiboot_info_t* mi = (multiboot_info_t*) X86_PHYS_TO_VIRT(_multiboot_info);
        printf("multiboot: info @ %p flags %#x\n", mi, mi->flags);

        if ((mi->flags & MB_INFO_CMD_LINE) && mi->cmdline && (!verify)) {
            const char* cmdline = (const char*) X86_PHYS_TO_VIRT(mi->cmdline);
            printf("multiboot: cmdline @ %p\n", cmdline);
            cmdline_append(cmdline);
        }
        if ((mi->flags & MB_INFO_MODS) && mi->mods_addr) {
            module_t* mod = (module_t*) X86_PHYS_TO_VIRT(mi->mods_addr);
            if (mi->mods_count > 0) {
                printf("multiboot: ramdisk @ %08x..%08x\n", mod->mod_start, mod->mod_end);
                process_bootdata(reinterpret_cast<bootdata_t*>(X86_PHYS_TO_VIRT(mod->mod_start)),
                                 mod->mod_start, verify);
            }
        }
    }
    if (_bootdata_base != NULL) {
        bootdata_t* bd = (bootdata_t*) X86_PHYS_TO_VIRT(_bootdata_base);
        process_bootdata(bd, (uintptr_t) _bootdata_base, verify);
    }

    halt_on_panic = cmdline_get_bool("kernel.halt-on-panic", false);
}

static void* ramdisk_base;
static size_t ramdisk_size;

static void platform_preserve_ramdisk(void) {
    if (bootloader.ramdisk_size == 0) {
        return;
    }
    if (bootloader.ramdisk_base == 0) {
        return;
    }
    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = ROUNDUP_PAGE_SIZE(bootloader.ramdisk_size) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(bootloader.ramdisk_base, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }

    ramdisk_base = paddr_to_kvaddr(bootloader.ramdisk_base);
    ramdisk_size = pages * PAGE_SIZE;
}

void* platform_get_ramdisk(size_t *size) {
    if (ramdisk_base) {
        *size = ramdisk_size;
        return ramdisk_base;
    } else {
        *size = 0;
        return NULL;
    }
}

#include <dev/display.h>
#include <lib/gfxconsole.h>

status_t display_get_info(struct display_info *info) {
    return gfxconsole_display_get_info(info);
}

static void platform_early_display_init(void) {
    struct display_info info;
    void *bits;

    if (bootloader.fb.base == 0) {
        return;
    }

    if (cmdline_get_bool("gfxconsole.early", false) == false) {
        early_console_disabled = true;
        return;
    }

    // allocate an offscreen buffer of worst-case size, page aligned
    bits = boot_alloc_mem(8192 + bootloader.fb.height * bootloader.fb.stride * 4);
    bits = (void*) ((((uintptr_t) bits) + 4095) & (~4095));

    memset(&info, 0, sizeof(info));
    info.format = bootloader.fb.format;
    info.width = bootloader.fb.width;
    info.height = bootloader.fb.height;
    info.stride = bootloader.fb.stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    info.framebuffer = (void*) X86_PHYS_TO_VIRT(bootloader.fb.base);

    gfxconsole_bind_display(&info, bits);
}

/* Ensure the framebuffer is write-combining as soon as we have the VMM.
 * Some system firmware has the MTRRs for the framebuffer set to Uncached.
 * Since dealing with MTRRs is rather complicated, we wait for the VMM to
 * come up so we can use PAT to manage the memory types. */
static void platform_ensure_display_memtype(uint level)
{
    if (bootloader.fb.base == 0) {
        return;
    }
    if (early_console_disabled) {
        return;
    }
    struct display_info info;
    memset(&info, 0, sizeof(info));
    info.format = bootloader.fb.format;
    info.width = bootloader.fb.width;
    info.height = bootloader.fb.height;
    info.stride = bootloader.fb.stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;

    void *addr = NULL;
    status_t status = VmAspace::kernel_aspace()->AllocPhysical(
            "boot_fb",
            ROUNDUP(info.stride * info.height * 4, PAGE_SIZE),
            &addr,
            PAGE_SIZE_SHIFT,
            bootloader.fb.base,
            0 /* vmm flags */,
            ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);
    if (status != MX_OK) {
        TRACEF("Failed to map boot_fb: %d\n", status);
        return;
    }

    info.framebuffer = addr;
    gfxconsole_bind_display(&info, NULL);
}
LK_INIT_HOOK(display_memtype, &platform_ensure_display_memtype, LK_INIT_LEVEL_VM + 1);

static efi_guid magenta_guid = MAGENTA_VENDOR_GUID;
static char16_t crashlog_name[] = MAGENTA_CRASHLOG_EFIVAR;

static fbl::RefPtr<VmAspace> efi_aspace;

typedef struct {
    uint64_t magic;
    uint64_t length;
    uint64_t nmagic;
    uint64_t nlength;
} log_hdr_t;

#define NVRAM_MAGIC (0x6f8962d66b28504fULL)

static size_t nvram_stow_crashlog(void* log, size_t len) {
    size_t max = bootloader.nvram.length - sizeof(log_hdr_t);
    void* nvram = paddr_to_kvaddr(bootloader.nvram.base);
    if (nvram == NULL) {
        return 0;
    }

    if (log == NULL) {
        return max;
    }
    if (len > max) {
        len = max;
    }

    log_hdr_t hdr = {
        .magic = NVRAM_MAGIC,
        .length = len,
        .nmagic = ~NVRAM_MAGIC,
        .nlength = ~len,
    };
    memcpy(nvram, &hdr, sizeof(hdr));
    memcpy(static_cast<char*>(nvram) + sizeof(hdr), log, len);
    arch_clean_cache_range((uintptr_t)nvram, sizeof(hdr) + len);
    return len;
}

static size_t nvram_recover_crashlog(size_t len, void* cookie,
                                     void (*func)(const void* data, size_t, size_t len, void* cookie)) {
    size_t max = bootloader.nvram.length - sizeof(log_hdr_t);
    void* nvram = paddr_to_kvaddr(bootloader.nvram.base);
    if (nvram == NULL) {
        return 0;
    }
    log_hdr_t hdr;
    memcpy(&hdr, nvram, sizeof(hdr));
    if ((hdr.magic != NVRAM_MAGIC) || (hdr.length > max) ||
        (hdr.nmagic != ~NVRAM_MAGIC) || (hdr.nlength != ~hdr.length)) {
        printf("nvram-crashlog: bad header: %016lx %016lx %016lx %016lx\n",
               hdr.magic, hdr.length, hdr.nmagic, hdr.nlength);
        return 0;
    }
    if (len == 0) {
        return hdr.length;
    }
    if (len > hdr.length) {
        len = hdr.length;
    }
    func(static_cast<char*>(nvram) + sizeof(hdr), 0, len, cookie);

    // invalidate header so we don't get a stale crashlog
    // on future boots
    hdr.magic = 0;
    memcpy(nvram, &hdr, sizeof(hdr));
    return hdr.length;
}

void platform_init_crashlog(void) {
    if (bootloader.nvram.base && bootloader.nvram.length > sizeof(log_hdr_t)) {
        // Nothing to do for simple nvram logs
        return;
    } else {
        bootloader.nvram.base = 0;
        bootloader.nvram.length = 0;
    }

    if (bootloader.efi_system_table != NULL) {
        // Create a linear mapping to use to call UEFI Runtime Services
        efi_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "uefi");
        if (!efi_aspace) {
            return;
        }

        //TODO: get more precise about this.  This gets the job done on
        //      the platforms we're working on right now, but is probably
        //      not entirely correct.
        void* ptr = (void*) 0;
        mx_status_t r = efi_aspace->AllocPhysical("1:1", 16*1024*1024*1024UL, &ptr,
                                                  PAGE_SIZE_SHIFT, 0,
                                                  VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
                                                  ARCH_MMU_FLAG_PERM_READ |
                                                  ARCH_MMU_FLAG_PERM_WRITE |
                                                  ARCH_MMU_FLAG_PERM_EXECUTE);

        if (r != MX_OK) {
            efi_aspace.reset();
        }
    }
}

// Something big enough for the panic log but not too enormous
// to avoid excessive pressure on efi variable storage
#define MAX_EFI_CRASHLOG_LEN 4096

static size_t efi_stow_crashlog(void* log, size_t len) {
    if (!efi_aspace) {
        return 0;
    }
    if (log == NULL) {
        return MAX_EFI_CRASHLOG_LEN;
    }
    if (len > MAX_EFI_CRASHLOG_LEN) {
        len = MAX_EFI_CRASHLOG_LEN;
    }

    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(efi_aspace.get()));

    efi_system_table* sys = static_cast<efi_system_table*>(bootloader.efi_system_table);
    efi_runtime_services* rs = sys->RuntimeServices;
    if (rs->SetVariable(crashlog_name, &magenta_guid, MAGENTA_CRASHLOG_EFIATTR, len, log) == 0) {
        return len;
    } else {
        return 0;
    }
}

size_t platform_stow_crashlog(void* log, size_t len) {
    if (bootloader.nvram.base) {
        return nvram_stow_crashlog(log, len);
    } else {
        return efi_stow_crashlog(log, len);
    }
}

size_t platform_recover_crashlog(size_t len, void* cookie,
                                 void (*func)(const void* data, size_t, size_t len, void* cookie)) {
    if (bootloader.nvram.base != 0) {
        return nvram_recover_crashlog(len, cookie, func);
    } else {
        return 0;
    }
}

typedef struct e820_walk_ctx {
    uint8_t* buf;
    size_t len;
    mx_status_t ret;
} e820_walk_ctx_t;

static void e820_entry_walk(uint64_t base, uint64_t size, bool is_mem, void* void_ctx) {
    e820_walk_ctx* ctx = (e820_walk_ctx*)void_ctx;

    // Something went wrong in one of the previous calls, don't attempt to
    // continue.
    if (ctx->ret != MX_OK)
        return;

    // Make sure we have enough space in the buffer.
    if (ctx->len < sizeof(e820entry_t)) {
        ctx->ret = MX_ERR_BUFFER_TOO_SMALL;
        return;
    }

    e820entry_t* entry = (e820entry_t*)ctx->buf;
    entry->addr = base;
    entry->size = size;

    // Hack: When we first parse this map we normalize each section to either
    // memory or not-memory. When we pass it to the next kernel, we lose
    // information about the type of "not memory" in each region.
    entry->type = is_mem ? E820_RAM : E820_RESERVED;

    ctx->buf += sizeof(*entry);
    ctx->len -= sizeof(*entry);
    ctx->ret = MX_OK;
}

// Give the platform an opportunity to append any platform specific bootdata
// sections.
mx_status_t platform_mexec_patch_bootdata(uint8_t* bootdata, const size_t len) {
    uint8_t e820buf[sizeof(e820entry_t) * 32];

    e820_walk_ctx ctx;
    ctx.buf = e820buf;
    ctx.len = sizeof(e820buf);
    ctx.ret = MX_OK;

    mx_status_t ret = enumerate_e820(e820_entry_walk, &ctx);

    if (ret != MX_OK)
        return ret;

    if (ctx.ret != MX_OK)
        return ctx.ret;

    uint32_t section_length = (uint32_t)(sizeof(e820buf) - ctx.len);

    ret = bootdata_append_section(bootdata, len, e820buf, section_length,
                                  BOOTDATA_E820_TABLE, 0, 0);

    if (ret != MX_OK)
        return ret;

    // Append information about the framebuffer to the bootdata
    if (bootloader.fb.base) {
        ret = bootdata_append_section(bootdata, len, (uint8_t*)&bootloader.fb,
                                      sizeof(bootloader.fb), BOOTDATA_FRAMEBUFFER, 0, 0);
        if (ret != MX_OK)
            return ret;
    }

    if (bootloader.efi_system_table) {
        ret = bootdata_append_section(bootdata, len, (uint8_t*)&bootloader.efi_system_table,
                                      sizeof(bootloader.efi_system_table),
                                      BOOTDATA_EFI_SYSTEM_TABLE, 0, 0);
        if (ret != MX_OK)
            return ret;
    }

    if (bootloader.acpi_rsdp) {
        ret = bootdata_append_section(bootdata, len, (uint8_t*)&bootloader.acpi_rsdp,
                                      sizeof(bootloader.acpi_rsdp), BOOTDATA_ACPI_RSDP, 0, 0);
        if (ret != MX_OK)
            return ret;
    }

    return MX_OK;
}

// Number of pages required to identity map 4GiB of memory.
const size_t kBytesToIdentityMap = 4ull * GB;
const size_t kNumL2PageTables = kBytesToIdentityMap / (2ull * MB * NO_OF_PT_ENTRIES);
const size_t kNumL3PageTables = 1;
const size_t kNumL4PageTables = 1;
const size_t kTotalPageTableCount = kNumL2PageTables + kNumL3PageTables + kNumL4PageTables;

// Allocate `count` pages where no page has a physical address less than
// `lower_bound`
static void alloc_pages_greater_than(paddr_t lower_bound, size_t count, paddr_t* paddrs) {
    struct list_node list = LIST_INITIAL_VALUE(list);
    while (count) {
        const size_t actual = pmm_alloc_range(lower_bound, count, &list);

        for (size_t i = 0; i < actual; i++) {
            paddrs[count - (i + 1)] = lower_bound + PAGE_SIZE * i;
        }

        count -= actual;
        lower_bound += PAGE_SIZE * (actual + 1);

        // If we're past the 4GiB mark and still trying to allocate, just give
        // up.
        if (lower_bound >= (4 * GB)) {
            panic("failed to allocate page tables for mexec");
        }
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops,
                    uintptr_t new_bootimage_addr, size_t new_bootimage_len) {

    // A hacky way to handle disabling all PCI devices until we have devhost
    // lifecycles implemented
    PcieBusDriver::GetDriver()->DisableBus();

    // This code only handles one L3 and one L4 page table for now. Fail if
    // there are more L2 page tables than can fit in one L3 page table.
    static_assert(kNumL2PageTables <= NO_OF_PT_ENTRIES,
                  "Kexec identity map size is too large. Only one L3 PTE is supported at this time.");
    static_assert(kNumL3PageTables == 1, "Only 1 L3 page table is supported at this time.");
    static_assert(kNumL4PageTables == 1, "Only 1 L4 page table is supported at this time.");

    // Identity map the first 4GiB of RAM
    fbl::RefPtr<VmAspace> identity_aspace =
            VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "x86-64 mexec 1:1");
    DEBUG_ASSERT(identity_aspace);

    const uint perm_flags_rwx = ARCH_MMU_FLAG_PERM_READ  |
                                ARCH_MMU_FLAG_PERM_WRITE |
                                ARCH_MMU_FLAG_PERM_EXECUTE;
    void* identity_address = 0x0;
    paddr_t pa = 0;
    mx_status_t result = identity_aspace->AllocPhysical("1:1 mapping", kBytesToIdentityMap,
                                            &identity_address, 0, pa,
                                            VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
                                            perm_flags_rwx);
    if (result != MX_OK) {
        panic("failed to identity map low memory");
    }

    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(identity_aspace.get()));

    paddr_t safe_pages[kTotalPageTableCount];
    alloc_pages_greater_than(new_bootimage_addr + new_bootimage_len + PAGE_SIZE,
                             kTotalPageTableCount, safe_pages);

    size_t safe_page_id = 0;
    volatile pt_entry_t* ptl4 = (pt_entry_t*)paddr_to_kvaddr(safe_pages[safe_page_id++]);
    volatile pt_entry_t* ptl3 = (pt_entry_t*)paddr_to_kvaddr(safe_pages[safe_page_id++]);

    // Initialize these to 0
    for (size_t i = 0; i < NO_OF_PT_ENTRIES; i++) {
        ptl4[i] = 0;
        ptl3[i] = 0;
    }

    for (size_t i = 0; i < kNumL2PageTables; i++) {
        ptl3[i] = safe_pages[safe_page_id] | X86_KERNEL_PD_FLAGS;
        volatile pt_entry_t* ptl2 = (pt_entry_t*)paddr_to_kvaddr(safe_pages[safe_page_id]);

        for (size_t j = 0; j < NO_OF_PT_ENTRIES; j++) {
            ptl2[j] = (2 * MB * (i * NO_OF_PT_ENTRIES + j)) | X86_KERNEL_PD_LP_FLAGS;
        }

        safe_page_id++;
    }

    ptl4[0] = vaddr_to_paddr((void*)ptl3) | X86_KERNEL_PD_FLAGS;

    mexec_assembly((uintptr_t)new_bootimage_addr, vaddr_to_paddr((void*)ptl4), 0, 0, ops, 0);
}

void platform_halt_secondary_cpus(void)
{
    // Migrate this thread to the boot cpu.
    thread_migrate_cpu(BOOT_CPU_ID);

    // Send a shutdown interrupt to all the other cores.
    apic_send_broadcast_ipi(0x00, DELIVERY_MODE_INIT);
}

void platform_early_init(void)
{
    /* extract bootloader data while still accessible */
    /* this includes debug uart config, etc. */
    platform_save_bootloader_data(false);

    /* get the debug output working */
    platform_init_debug_early();

#if WITH_LEGACY_PC_CONSOLE
    /* get the text console working */
    platform_init_console();
#endif

    /* if the bootloader has framebuffer info, use it for early console */
    platform_early_display_init();

#if DEBUG_BOOT_DATA
    // second pass to verify crc32s so we can see the results
    platform_save_bootloader_data(true);
#endif

    /* initialize physical memory arenas */
    platform_mem_init();

    platform_preserve_ramdisk();
}

static void platform_init_smp(void)
{
    uint32_t num_cpus = 0;

    status_t status = platform_enumerate_cpus(NULL, 0, &num_cpus);
    if (status != MX_OK) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        return;
    }

    // allocate 2x the table for temporary work
    uint32_t *apic_ids = static_cast<uint32_t *>(malloc(sizeof(*apic_ids) * num_cpus * 2));
    if (!apic_ids) {
        TRACEF("failed to allocate apic_ids table, disabling SMP\n");
        return;
    }

    // a temporary list used before we filter out hyperthreaded pairs
    uint32_t *apic_ids_temp = apic_ids + num_cpus;

    // find the list of all cpu apic ids into a temporary list
    uint32_t real_num_cpus;
    status = platform_enumerate_cpus(apic_ids_temp, num_cpus, &real_num_cpus);
    if (status != MX_OK || num_cpus != real_num_cpus) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        free(apic_ids);
        return;
    }

    // Filter out hyperthreads if we've been told not to init them
    bool use_ht = cmdline_get_bool("kernel.smp.ht", true);

    // we're implicitly running on the BSP
    uint32_t bsp_apic_id = apic_local_id();

    // iterate over all the cores and optionally disable some of them
    dprintf(INFO, "cpu topology:\n");
    uint32_t using_count = 0;
    for (uint32_t i = 0; i < num_cpus; ++i) {
        x86_cpu_topology_t topo;
        x86_cpu_topology_decode(apic_ids_temp[i], &topo);

        // filter it out if it's a HT pair that we dont want to use
        bool keep = true;
        if (!use_ht && topo.smt_id != 0)
            keep = false;

        dprintf(INFO, "\t%u: apic id 0x%x package %u core %u smt %u%s%s\n",
                i, apic_ids_temp[i], topo.package_id, topo.core_id, topo.smt_id,
                (apic_ids_temp[i] == bsp_apic_id) ? " BSP" : "",
                keep ? "" : " (not using)");

        if (!keep)
            continue;

        // save this apic id into the primary list
        apic_ids[using_count++] = apic_ids_temp[i];
    }
    num_cpus = using_count;

    // Find the CPU count limit
    uint32_t max_cpus = cmdline_get_uint32("kernel.smp.maxcpus", SMP_MAX_CPUS);
    if (max_cpus > SMP_MAX_CPUS || max_cpus <= 0) {
        printf("invalid kernel.smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
        max_cpus = SMP_MAX_CPUS;
    }

    dprintf(INFO, "Found %u cpu%c\n", num_cpus, (num_cpus > 1) ? 's': ' ');
    if (num_cpus > max_cpus) {
        dprintf(INFO, "Clamping number of CPUs to %u\n", max_cpus);
        num_cpus = max_cpus;
    }

    if (num_cpus == max_cpus || !use_ht) {
        // If we are at the max number of CPUs, or have filtered out
        // hyperthreads, sanity check that the bootstrap processor is in the set.
        bool found_bp = false;
        for (unsigned int i = 0; i < num_cpus; ++i) {
            if (apic_ids[i] == bsp_apic_id) {
                found_bp = true;
                break;
            }
        }
        ASSERT(found_bp);
    }

    x86_init_smp(apic_ids, num_cpus);

    // trim the boot cpu out of the apic id list before passing to the AP booting routine
    for (uint i = 0; i < num_cpus - 1; ++i) {
        if (apic_ids[i] == bsp_apic_id) {
            memmove(&apic_ids[i], &apic_ids[i+1], sizeof(*apic_ids) * (num_cpus - i - 1));
            break;
        }
    }

    x86_bringup_aps(apic_ids, num_cpus - 1);

    free(apic_ids);
}

status_t platform_mp_prep_cpu_unplug(uint cpu_id)
{
    // TODO: Make sure the IOAPIC and PCI have nothing for this CPU
    return arch_mp_prep_cpu_unplug(cpu_id);
}

void platform_init(void)
{
    platform_init_debug();

    platform_init_crashlog();

#if NO_USER_KEYBOARD
    platform_init_keyboard(&console_input_buf);
#endif

    platform_init_smp();
}
