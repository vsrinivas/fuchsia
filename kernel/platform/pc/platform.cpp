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
#include <arch/x86/apic.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/mmu.h>
#include <platform.h>
#include "platform_p.h"
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/pc/bootloader.h>
#include <platform/pc/memory.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <magenta/boot/bootdata.h>
#include <magenta/boot/multiboot.h>
#include <dev/uart.h>
#include <arch/mmu.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/vm/vm_aspace.h>

extern "C" {
#include <efi/runtime-services.h>
#include <efi/system-table.h>
};


#define LOCAL_TRACE 0

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
        if (bd->length < sizeof(uint64_t)) {
            break;
        }
        bootloader.acpi_rsdp = *((uint64_t*)item);
        break;
    case BOOTDATA_EFI_SYSTEM_TABLE:
        if (bd->length < sizeof(uint64_t)) {
            break;
        }
        bootloader.efi_system_table = (void*) *((uint64_t*)item);
        break;
    case BOOTDATA_FRAMEBUFFER: {
        if (bd->length < sizeof(bootdata_swfb_t)) {
            break;
        }
        bootdata_swfb_t* fb = static_cast<bootdata_swfb_t*>(item);
        bootloader.fb_base = (uint32_t) fb->phys_base;
        bootloader.fb_width = fb->width;
        bootloader.fb_height = fb->height;
        bootloader.fb_stride = fb->stride;
        bootloader.fb_format = fb->format;
        break;
    }
    case BOOTDATA_CMDLINE:
        if (bd->length < 1) {
            break;
        }
        ((char*) item)[bd->length - 1] = 0;
        cmdline_append((char*) item);
        break;
    case BOOTDATA_EFI_MEMORY_MAP:
        bootloader.efi_mmap = item;
        bootloader.efi_mmap_size = bd->length;
        break;
    case BOOTDATA_E820_TABLE:
        bootloader.e820_table = item;
        bootloader.e820_count = bd->length / sizeof(e820entry_t);
        break;
    case BOOTDATA_IGNORE:
        break;
    }
    return 0;
}

extern "C" void *boot_alloc_mem(size_t len);
extern "C" void boot_alloc_reserve(uintptr_t phys, size_t _len);

static void process_bootdata(bootdata_t* hdr, uintptr_t phys) {
    if ((hdr->type != BOOTDATA_CONTAINER) ||
        (hdr->extra != BOOTDATA_MAGIC) ||
        (hdr->flags != 0)) {
        printf("bootdata: invalid %08x %08x %08x %08x\n",
               hdr->type, hdr->length, hdr->extra, hdr->flags);
        return;
    }

    size_t total_len = hdr->length + sizeof(*hdr);

    printf("bootdata: @ %p (%zu bytes)\n", hdr, total_len);

    bootdata_t* bd = hdr + 1;
    size_t remain = hdr->length;
    while (remain > sizeof(bootdata_t)) {
        remain -= sizeof(bootdata_t);
        uintptr_t item = reinterpret_cast<uintptr_t>(bd + 1);
        size_t len = BOOTDATA_ALIGN(bd->length);
        if (len > remain) {
            printf("bootdata: truncated\n");
            break;
        }
        if (process_bootitem(bd, reinterpret_cast<void*>(item))) {
            break;
        }
        bd = reinterpret_cast<bootdata_t*>(item + len);
        remain -= len;
    }

    boot_alloc_reserve(phys, total_len);
    bootloader.ramdisk_base = phys;
    bootloader.ramdisk_size = total_len;
}

extern bool halt_on_panic;

static void platform_save_bootloader_data(void) {
    if (_multiboot_info != NULL) {
        multiboot_info_t* mi = (multiboot_info_t*) X86_PHYS_TO_VIRT(_multiboot_info);
        printf("multiboot: info @ %p\n", mi);

        if ((mi->flags & MB_INFO_CMD_LINE) && mi->cmdline) {
            const char* cmdline = (const char*) X86_PHYS_TO_VIRT(mi->cmdline);
            printf("multiboot: cmdline @ %p\n", cmdline);
            cmdline_append(cmdline);
        }
        if ((mi->flags & MB_INFO_MODS) && mi->mods_addr) {
            module_t* mod = (module_t*) X86_PHYS_TO_VIRT(mi->mods_addr);
            if (mi->mods_count > 0) {
                printf("multiboot: ramdisk @ %08x..%08x\n", mod->mod_start, mod->mod_end);
                process_bootdata(reinterpret_cast<bootdata_t*>(X86_PHYS_TO_VIRT(mod->mod_start)),
                                 mod->mod_start);
            }
        }
    }
    if (_bootdata_base != NULL) {
        bootdata_t* bd = (bootdata_t*) X86_PHYS_TO_VIRT(_bootdata_base);
        process_bootdata(bd, (uintptr_t) _bootdata_base);
    }

    halt_on_panic = cmdline_get_bool("kernel.halt_on_panic", false);
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

    if (bootloader.fb_base == 0) {
        return;
    }

    if (cmdline_get_bool("gfxconsole.early", false) == false) {
        early_console_disabled = true;
        return;
    }

    // allocate an offscreen buffer of worst-case size, page aligned
    bits = boot_alloc_mem(8192 + bootloader.fb_height * bootloader.fb_stride * 4);
    bits = (void*) ((((uintptr_t) bits) + 4095) & (~4095));

    memset(&info, 0, sizeof(info));
    info.format = bootloader.fb_format;
    info.width = bootloader.fb_width;
    info.height = bootloader.fb_height;
    info.stride = bootloader.fb_stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    info.framebuffer = (void*) X86_PHYS_TO_VIRT(bootloader.fb_base);

    gfxconsole_bind_display(&info, bits);
}

/* Ensure the framebuffer is write-combining as soon as we have the VMM.
 * Some system firmware has the MTRRs for the framebuffer set to Uncached.
 * Since dealing with MTRRs is rather complicated, we wait for the VMM to
 * come up so we can use PAT to manage the memory types. */
static void platform_ensure_display_memtype(uint level)
{
    if (bootloader.fb_base == 0) {
        return;
    }
    if (early_console_disabled) {
        return;
    }
    struct display_info info;
    memset(&info, 0, sizeof(info));
    info.format = bootloader.fb_format;
    info.width = bootloader.fb_width;
    info.height = bootloader.fb_height;
    info.stride = bootloader.fb_stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;

    void *addr = NULL;
    status_t status = VmAspace::kernel_aspace()->AllocPhysical(
            "boot_fb",
            ROUNDUP(info.stride * info.height * 4, PAGE_SIZE),
            &addr,
            PAGE_SIZE_SHIFT,
            bootloader.fb_base,
            0 /* vmm flags */,
            ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);
    if (status != NO_ERROR) {
        TRACEF("Failed to map boot_fb: %d\n", status);
        return;
    }

    info.framebuffer = addr;
    gfxconsole_bind_display(&info, NULL);
}
LK_INIT_HOOK(display_memtype, &platform_ensure_display_memtype, LK_INIT_LEVEL_VM + 1);

static efi_guid magenta_guid = MAGENTA_VENDOR_GUID;
static char16_t crashlog_name[] = MAGENTA_CRASHLOG_EFIVAR;

static mxtl::RefPtr<VmAspace> efi_aspace;

void platform_init_crashlog(void) {
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
                                                  VMM_FLAG_VALLOC_SPECIFIC,
                                                  ARCH_MMU_FLAG_PERM_READ |
                                                  ARCH_MMU_FLAG_PERM_WRITE |
                                                  ARCH_MMU_FLAG_PERM_EXECUTE);

        if (r != NO_ERROR) {
            efi_aspace.reset();
        }
    }
}

// Something big enough for the panic log but not too enormous
// to avoid excessive pressure on efi variable storage
#define MAX_CRASHLOG_LEN 4096

size_t platform_stow_crashlog(void* log, size_t len) {
    if (!efi_aspace) {
        return 0;
    }
    if (log == NULL) {
        return MAX_CRASHLOG_LEN;
    }
    if (len > MAX_CRASHLOG_LEN) {
        len = MAX_CRASHLOG_LEN;
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

void platform_early_init(void)
{
    /* get the debug output working */
    platform_init_debug_early();

#if WITH_LEGACY_PC_CONSOLE
    /* get the text console working */
    platform_init_console();
#endif

    /* extract bootloader data while still accessible */
    platform_save_bootloader_data();

    /* if the bootloader has framebuffer info, use it for early console */
    platform_early_display_init();

    /* initialize physical memory arenas */
    platform_mem_init();

    platform_preserve_ramdisk();
}

#if WITH_SMP
static void platform_init_smp(void)
{
    uint32_t num_cpus = 0;

    status_t status = platform_enumerate_cpus(NULL, 0, &num_cpus);
    if (status != NO_ERROR) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        return;
    }

    // allocate 2x the table for temporary work
    uint32_t *apic_ids = static_cast<uint32_t *>(malloc(sizeof(*apic_ids) * num_cpus * 2));
    if (apic_ids == NULL) {
        TRACEF("failed to allocate apic_ids table, disabling SMP\n");
        return;
    }

    // a temporary list used before we filter out hyperthreaded pairs
    uint32_t *apic_ids_temp = apic_ids + num_cpus;

    // find the list of all cpu apic ids into a temporary list
    uint32_t real_num_cpus;
    status = platform_enumerate_cpus(apic_ids_temp, num_cpus, &real_num_cpus);
    if (status != NO_ERROR || num_cpus != real_num_cpus) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        free(apic_ids);
        return;
    }

    // Filter out hyperthreads if we've been told not to init them
    bool use_ht = cmdline_get_bool("smp.ht", true);

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
    uint32_t max_cpus = cmdline_get_uint32("smp.maxcpus", SMP_MAX_CPUS);
    if (max_cpus > SMP_MAX_CPUS || max_cpus <= 0) {
        printf("invalid smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
        max_cpus = SMP_MAX_CPUS;
    }

    dprintf(INFO, "Found %u cpus\n", num_cpus);
    if (num_cpus > max_cpus) {
        TRACEF("Clamping number of CPUs to %u\n", max_cpus);
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

    for (uint i = 0; i < num_cpus - 1; ++i) {
        if (apic_ids[i] == bsp_apic_id) {
            apic_ids[i] = apic_ids[num_cpus - 1];
            apic_ids[num_cpus - 1] = bsp_apic_id;
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

#endif

void platform_init(void)
{
    platform_init_debug();

    platform_init_crashlog();

#if NO_USER_KEYBOARD
    platform_init_keyboard(&console_input_buf);
#endif

#if WITH_SMP
    platform_init_smp();
#endif
}
