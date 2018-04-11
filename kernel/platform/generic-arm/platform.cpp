// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <reg.h>
#include <trace.h>

#include <arch.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/power.h>
#include <dev/psci.h>
#include <dev/uart.h>
#include <kernel/cmdline.h>
#include <kernel/spinlock.h>
#include <lk/init.h>
#include <vm/physmap.h>
#include <vm/vm.h>

#include <mexec.h>
#include <platform.h>

#include <target.h>

#include <arch/arm64.h>
#include <arch/arm64/mmu.h>
#include <arch/arm64/mp.h>
#include <arch/arm64/periphmap.h>
#include <arch/mp.h>

#include <vm/vm_aspace.h>
#include <vm/bootreserve.h>

#include <lib/console.h>
#include <lib/memory_limit.h>
#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif
#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <pdev/pdev.h>
#include <zircon/boot/bootdata.h>
#include <zircon/types.h>

// Defined in start.S.
extern paddr_t kernel_entry_paddr;
extern paddr_t bootdata_paddr;

static void* ramdisk_base;
static size_t ramdisk_size;

static uint cpu_cluster_count = 0;
static uint cpu_cluster_cpus[SMP_CPU_MAX_CLUSTERS] = {0};

static bool halt_on_panic = false;
static bool uart_disabled = false;

// all of the configured memory arenas from the bootdata
// at the moment, only support 1 arena
static pmm_arena_info_t mem_arena = {
    /* .name */ "sdram",
    /* .flags */ PMM_ARENA_FLAG_KMAP,
    /* .priority */ 0,
    /* .base */ 0, // filled in by bootdata
    /* .size */ 0, // filled in by bootdata
};

// bootdata to save for mexec
// TODO(voydanoff): more generic way of doing this that can be shared with PC platform
static uint8_t mexec_bootdata[4096];
static size_t mexec_bootdata_length = 0;

static volatile int panic_started;

static void halt_other_cpus(void) {
    static volatile int halted = 0;

    if (atomic_swap(&halted, 1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, MP_IPI_HALT);

        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            __asm volatile("nop");
        }
    }
}

void platform_panic_start(void) {
    arch_disable_ints();

    halt_other_cpus();

    if (atomic_swap(&panic_started, 1) == 0) {
#if WITH_LIB_DEBUGLOG
        dlog_bluescreen_init();
#endif
    }
}

void* platform_get_ramdisk(size_t* size) {
    if (ramdisk_base) {
        *size = ramdisk_size;
        return ramdisk_base;
    } else {
        *size = 0;
        return nullptr;
    }
}

void platform_halt_cpu(void) {
    psci_cpu_off();
}

// One of these threads is spun up per CPU and calls halt which does not return.
static int park_cpu_thread(void* arg) {
    event_t* shutdown_cplt = (event_t*)arg;

    mp_set_curr_cpu_online(false);
    mp_set_curr_cpu_active(false);

    arch_disable_ints();

    // Let the thread on the boot CPU know that we're just about done shutting down.
    event_signal(shutdown_cplt, true);

    // This method will not return because the target cpu has halted.
    platform_halt_cpu();

    panic("control should never reach here");
    return -1;
}

void platform_halt_secondary_cpus(void) {
    // Make sure that the current thread is pinned to the boot cpu.
    const thread_t* current_thread = get_current_thread();
    DEBUG_ASSERT(current_thread->cpu_affinity == (1 << BOOT_CPU_ID));

    // Threads responsible for parking the cores.
    thread_t* park_thread[SMP_MAX_CPUS];

    // These are signalled when the CPU has almost shutdown.
    event_t shutdown_cplt[SMP_MAX_CPUS];

    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        // The boot cpu is going to be performing the remainder of the mexec
        // for us so we don't want to park that one.
        if (i == BOOT_CPU_ID) {
            continue;
        }

        event_init(&shutdown_cplt[i], false, 0);

        char park_thread_name[20];
        snprintf(park_thread_name, sizeof(park_thread_name), "park %u", i);
        park_thread[i] = thread_create(park_thread_name, park_cpu_thread,
                                       (void*)(&shutdown_cplt[i]), DEFAULT_PRIORITY,
                                       DEFAULT_STACK_SIZE);

        thread_set_cpu_affinity(park_thread[i], cpu_num_to_mask(i));
        thread_resume(park_thread[i]);
    }

    // Wait for all CPUs to signal that they're shutting down.
    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        if (i == BOOT_CPU_ID) {
            continue;
        }
        event_wait(&shutdown_cplt[i]);
    }

    // TODO(gkalsi): Wait for the secondaries to shutdown rather than sleeping.
    //               After the shutdown thread shuts down the core, we never
    //               hear from it again, so we wait 1 second to allow each
    //               thread to shut down. This is somewhat of a hack.
    thread_sleep_relative(ZX_SEC(1));
}

static void platform_start_cpu(uint cluster, uint cpu) {
    uint32_t ret = psci_cpu_on(cluster, cpu, kernel_entry_paddr);
    dprintf(INFO, "Trying to start cpu %u:%u returned: %d\n", cluster, cpu, (int)ret);
}

static void* allocate_one_stack(void) {
    uint8_t* stack = static_cast<uint8_t*>(
        pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, nullptr, nullptr));
    return static_cast<void*>(stack + ARCH_DEFAULT_STACK_SIZE);
}

static void platform_cpu_init(void) {
    for (uint cluster = 0; cluster < cpu_cluster_count; cluster++) {
        for (uint cpu = 0; cpu < cpu_cluster_cpus[cluster]; cpu++) {
            if (cluster != 0 || cpu != 0) {
                void* sp = allocate_one_stack();
                void* unsafe_sp = nullptr;
#if __has_feature(safe_stack)
                unsafe_sp = allocate_one_stack();
#endif
                arm64_set_secondary_sp(cluster, cpu, sp, unsafe_sp);
                platform_start_cpu(cluster, cpu);
            }
        }
    }
}

static inline bool is_bootdata_container(void* addr) {
    DEBUG_ASSERT(addr);

    bootdata_t* header = (bootdata_t*)addr;

    return header->type == BOOTDATA_CONTAINER;
}

static void save_mexec_bootdata(bootdata_t* section) {
    size_t length = BOOTDATA_ALIGN(section->length + sizeof(bootdata_t));
    ASSERT(sizeof(mexec_bootdata) - mexec_bootdata_length >= length);

    memcpy(&mexec_bootdata[mexec_bootdata_length], section, length);
    mexec_bootdata_length += length;
}

static void process_mem_range(const bootdata_mem_range_t* mem_range) {
    switch (mem_range->type) {
    case BOOTDATA_MEM_RANGE_RAM:
        if (mem_arena.size == 0) {
            mem_arena.base = mem_range->paddr;
            mem_arena.size = mem_range->length;
            dprintf(INFO, "mem_arena.base %#" PRIx64 " size %#" PRIx64 "\n", mem_arena.base,
                    mem_arena.size);
        } else {
            // if mem_area.base is already set, then just update the size
            mem_arena.size = mem_range->length;
            dprintf(INFO, "overriding mem arena 0 size from FDT: %#zx\n", mem_arena.size);
        }
        break;
    case BOOTDATA_MEM_RANGE_PERIPHERAL: {
        auto status = add_periph_range(mem_range->paddr, mem_range->length);
        ASSERT(status == ZX_OK);
        break;
    }
    case BOOTDATA_MEM_RANGE_RESERVED:
        dprintf(INFO, "boot reserve mem range: phys base %#" PRIx64 " length %#" PRIx64 "\n",
                mem_range->paddr, mem_range->length);
        boot_reserve_add_range(mem_range->paddr, mem_range->length);
        break;
    default:
        panic("bad mem_range->type in process_mem_range\n");
        break;
    }
}

static void process_bootsection(bootdata_t* section) {
    switch (section->type) {
    case BOOTDATA_KERNEL_DRIVER:
    case BOOTDATA_PLATFORM_ID:
        // we don't process these here, but we need to save them for mexec
        save_mexec_bootdata(section);
        break;
    case BOOTDATA_CMDLINE: {
        if (section->length < 1) {
            break;
        }
        char* contents = reinterpret_cast<char*>(section) + sizeof(bootdata_t);
        contents[section->length - 1] = '\0';
        cmdline_append(contents);
        break;
    }
    case BOOTDATA_MEM_CONFIG: {
        bootdata_mem_range_t* mem_range = reinterpret_cast<bootdata_mem_range_t*>(section + 1);
        uint32_t count = section->length / (uint32_t)sizeof(bootdata_mem_range_t);
        for (uint32_t i = 0; i < count; i++) {
            process_mem_range(mem_range++);
        }
        save_mexec_bootdata(section);
        break;
    }
    case BOOTDATA_CPU_CONFIG: {
        bootdata_cpu_config_t* cpu_config = reinterpret_cast<bootdata_cpu_config_t*>(section + 1);
        cpu_cluster_count = cpu_config->cluster_count;
        for (uint32_t i = 0; i < cpu_cluster_count; i++) {
            cpu_cluster_cpus[i] = cpu_config->clusters[i].cpu_count;
        }
        arch_init_cpu_map(cpu_cluster_count, cpu_cluster_cpus);
        save_mexec_bootdata(section);
        break;
    }
    }
}

static void process_bootdata(bootdata_t* root) {
    DEBUG_ASSERT(root);

    if (root->type != BOOTDATA_CONTAINER) {
        printf("bootdata: invalid type = %08x\n", root->type);
        return;
    }

    if (root->extra != BOOTDATA_MAGIC) {
        printf("bootdata: invalid magic = %08x\n", root->extra);
        return;
    }

    size_t offset = sizeof(bootdata_t);
    const size_t length = (root->length);

    if (!(root->flags & BOOTDATA_FLAG_V2)) {
        printf("bootdata: v1 no longer supported\n");
    }

    while (offset < length) {
        uintptr_t ptr = reinterpret_cast<const uintptr_t>(root);
        bootdata_t* section = reinterpret_cast<bootdata_t*>(ptr + offset);

        process_bootsection(section);
        offset += BOOTDATA_ALIGN(sizeof(bootdata_t) + section->length);
    }
}

void platform_early_init(void) {
    // if the bootdata_paddr variable is -1, it was not set
    // in start.S, so we are in a bad place.
    if (bootdata_paddr == -1UL) {
        panic("no bootdata_paddr!\n");
    }

    void* bootdata_vaddr = paddr_to_physmap(bootdata_paddr);

    // initialize the boot memory reservation system
    boot_reserve_init();

    if (bootdata_vaddr && is_bootdata_container(bootdata_vaddr)) {
        bootdata_t* header = (bootdata_t*)bootdata_vaddr;

        ramdisk_base = header;
        ramdisk_size = ROUNDUP(header->length + sizeof(*header), PAGE_SIZE);
    } else {
        panic("no bootdata!\n");
    }

    if (!ramdisk_base || !ramdisk_size) {
        panic("no ramdisk!\n");
    }

    bootdata_t* bootdata = reinterpret_cast<bootdata_t*>(ramdisk_base);
    // walk the bootdata structure and process all the entries
    process_bootdata(bootdata);

    // bring up kernel drivers after we have mapped our peripheral ranges
    pdev_init(bootdata);

    // Serial port should be active now

    // Read cmdline after processing bootdata, which may contain cmdline data.
    halt_on_panic = cmdline_get_bool("kernel.halt-on-panic", false);

    // Check if serial should be enabled
    const char* serial_mode = cmdline_get("kernel.serial");
    uart_disabled = (serial_mode != NULL && !strcmp(serial_mode, "none"));

    // add the ramdisk to the boot reserve memory list
    paddr_t ramdisk_start_phys = physmap_to_paddr(ramdisk_base);
    paddr_t ramdisk_end_phys = ramdisk_start_phys + ramdisk_size;
    dprintf(INFO, "reserving ramdisk phys range [%#" PRIx64 ", %#" PRIx64 "]\n",
            ramdisk_start_phys, ramdisk_end_phys - 1);
    boot_reserve_add_range(ramdisk_start_phys, ramdisk_size);

    // check if a memory limit was passed in via kernel.memory-limit-mb and
    // find memory ranges to use if one is found.
    mem_limit_ctx_t ctx;
    zx_status_t status = mem_limit_init(&ctx);
    if (status == ZX_OK) {
        // For these ranges we're using the base physical values
        ctx.kernel_base = get_kernel_base_phys();
        ctx.kernel_size = get_kernel_size();
        ctx.ramdisk_base = ramdisk_start_phys;
        ctx.ramdisk_size = ramdisk_end_phys - ramdisk_start_phys;

        // Figure out and add arenas based on the memory limit and our range of DRAM
        status = mem_limit_add_arenas_from_range(&ctx, mem_arena.base, mem_arena.size, mem_arena);
    }

    // If no memory limit was found, or adding arenas from the range failed, then add
    // the existing global arena.
    if (status != ZX_OK) {
        pmm_add_arena(&mem_arena);
    }

    // tell the boot allocator to mark ranges we've reserved as off limits
    boot_reserve_wire();
}

void platform_init(void) {
    platform_cpu_init();
}

// after the fact create a region to reserve the peripheral map(s)
static void platform_init_postvm(uint level) {
    reserve_periph_ranges();
}

LK_INIT_HOOK(platform_postvm, platform_init_postvm, LK_INIT_LEVEL_VM);

void platform_dputs_thread(const char* str, size_t len) {
    if (uart_disabled) {
        return;
    }
    uart_puts(str, len, true, true);
}

void platform_dputs_irq(const char* str, size_t len) {
    if (uart_disabled) {
        return;
    }
    uart_puts(str, len, false, true);
}

int platform_dgetc(char* c, bool wait) {
    if (uart_disabled) {
        return -1;
    }
    int ret = uart_getc(wait);
    if (ret == -1)
        return -1;
    *c = static_cast<char>(ret);
    return 0;
}

void platform_pputc(char c) {
    if (uart_disabled) {
        return;
    }
    uart_pputc(c);
}

int platform_pgetc(char* c, bool wait) {
    if (uart_disabled) {
        return -1;
    }
    int r = uart_pgetc();
    if (r == -1) {
        return -1;
    }

    *c = static_cast<char>(r);
    return 0;
}

/* stub out the hardware rng entropy generator, which doesn't exist on this platform */
size_t hw_rng_get_entropy(void* buf, size_t len, bool block) {
    return 0;
}

/* no built in framebuffer */
zx_status_t display_get_info(struct display_info* info) {
    return ZX_ERR_NOT_FOUND;
}

void platform_halt(platform_halt_action suggested_action, platform_halt_reason reason) {

    if (suggested_action == HALT_ACTION_REBOOT) {
        power_reboot(REBOOT_NORMAL);
        printf("reboot failed\n");
    } else if (suggested_action == HALT_ACTION_REBOOT_BOOTLOADER) {
        power_reboot(REBOOT_BOOTLOADER);
        printf("reboot-bootloader failed\n");
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        power_shutdown();
    }

#if WITH_LIB_DEBUGLOG
    thread_print_current_backtrace();
    dlog_bluescreen_halt();
#endif

    if (reason == HALT_REASON_SW_PANIC) {
        if (!halt_on_panic) {
            power_reboot(REBOOT_NORMAL);
            printf("reboot failed\n");
        }
#if ENABLE_PANIC_SHELL
        dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %d)\n", reason);
        arch_disable_ints();
        panic_shell_start();
#endif // ENABLE_PANIC_SHELL
    }

    dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", reason);

    // catch all fallthrough cases
    arch_disable_ints();
    for (;;)
        ;
}

size_t platform_stow_crashlog(void* log, size_t len) {
    return 0;
}

size_t platform_recover_crashlog(size_t len, void* cookie,
                                 void (*func)(const void* data, size_t, size_t len, void* cookie)) {
    return 0;
}

zx_status_t platform_mexec_patch_bootdata(uint8_t* bootdata, const size_t len) {
    size_t offset = 0;

    // copy certain bootdata sections provided by the bootloader or boot shim
    // to the mexec bootdata
    while (offset < mexec_bootdata_length) {
        bootdata_t* section = reinterpret_cast<bootdata_t*>(mexec_bootdata + offset);
        zx_status_t status;
        status = bootdata_append_section(bootdata, len, reinterpret_cast<uint8_t*>(section + 1),
                                         section->length, section->type, section->extra,
                                         section->flags);
        if (status != ZX_OK) return status;

        offset += BOOTDATA_ALIGN(sizeof(bootdata_t) + section->length);
    }

    return ZX_OK;
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops,
                    uintptr_t new_bootimage_addr, size_t new_bootimage_len,
                    uintptr_t entry64_addr) {
    paddr_t kernel_src_phys = (paddr_t)ops[0].src;
    paddr_t kernel_dst_phys = (paddr_t)ops[0].dst;

    // check to see if the kernel is packaged as a bootdata container
    bootdata_t* header = (bootdata_t *)paddr_to_physmap(kernel_src_phys);
    if (header[0].type == BOOTDATA_CONTAINER && header[1].type == BOOTDATA_KERNEL) {
        bootdata_kernel_t* kernel_header = (bootdata_kernel_t *)&header[2];
        // add offset from kernel header to entry point
        kernel_dst_phys += kernel_header->entry64;
    }
    // else just jump to beginning of kernel image

    mexec_assembly((uintptr_t)new_bootimage_addr, 0, 0, arm64_get_boot_el(), ops,
                  (void *)kernel_dst_phys);
}

bool platform_serial_enabled(void) {
    return !uart_disabled && uart_present();
}
