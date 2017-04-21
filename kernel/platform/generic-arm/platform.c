// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <err.h>
#include <debug.h>
#include <trace.h>

#include <dev/uart.h>
#include <arch.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/spinlock.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/psci.h>

#include <platform.h>
#include <dev/bcm28xx.h>

#include <target.h>

#include <arch/mp.h>
#include <arch/arm64/mp.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>

#include <lib/console.h>
#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif
#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <magenta/boot/bootdata.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/pdev.h>
#include <libfdt.h>

extern paddr_t boot_structure_paddr; // Defined in start.S.

static paddr_t ramdisk_start_phys = 0;
static paddr_t ramdisk_end_phys = 0;

static void* ramdisk_base;
static size_t ramdisk_size;

static uint cpu_cluster_count = 0;
static uint cpu_cluster_cpus[SMP_CPU_MAX_CLUSTERS] = {0};

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
 /* 1GB of sdram space */
 {
     .phys = MEMBASE,
     .virt = KERNEL_BASE,
     .size = MEMORY_APERTURE_SIZE,
     .flags = 0,
     .name = "memory"
 },

 /* peripherals */
 {
     .phys = PERIPH_BASE_PHYS,
     .virt = PERIPH_BASE_VIRT,
     .size = PERIPH_SIZE,
     .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
     .name = "peripherals"
 },
 /* null entry to terminate the list */
 {}
};

extern void arm_reset(void);

static pmm_arena_info_t arena = {
    .name = "sdram",
    .base = MEMBASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

static volatile int panic_started;

static void halt_other_cpus(void)
{
#if WITH_SMP
    static volatile int halted = 0;

    if (atomic_swap(&halted, 1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_CPU_ALL_BUT_LOCAL, MP_IPI_HALT);

        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            __asm volatile ("nop");
        }
    }
#endif
}

void platform_panic_start(void)
{
    arch_disable_ints();

    halt_other_cpus();

    if (atomic_swap(&panic_started, 1) == 0) {
#if WITH_LIB_DEBUGLOG
        dlog_bluescreen_init();
#endif
    }
}

// Reads Linux device tree to initialize command line and return ramdisk location
static void read_device_tree(void** ramdisk_base, size_t* ramdisk_size, size_t* mem_size) {
    if (ramdisk_base) *ramdisk_base = NULL;
    if (ramdisk_size) *ramdisk_size = 0;
    if (mem_size) *mem_size = 0;

    void* fdt = paddr_to_kvaddr(boot_structure_paddr);
    if (!fdt) {
        printf("%s: could not find device tree\n", __FUNCTION__);
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("%s fdt_check_header failed\n", __FUNCTION__);
        return;
    }

    int offset = fdt_path_offset(fdt, "/chosen");
    if (offset < 0) {
        printf("%s: fdt_path_offset(/chosen) failed\n", __FUNCTION__);
        return;
    }

    int length;
    const char* bootargs = fdt_getprop(fdt, offset, "bootargs", &length);
    if (bootargs) {
        printf("kernel command line: %s\n", bootargs);
        cmdline_init(bootargs);
    }

    if (ramdisk_base && ramdisk_size) {
        const void* ptr = fdt_getprop(fdt, offset, "linux,initrd-start", &length);
        if (ptr) {
            if (length == 4) {
                ramdisk_start_phys = fdt32_to_cpu(*(uint32_t *)ptr);
            } else if (length == 8) {
                ramdisk_start_phys = fdt64_to_cpu(*(uint64_t *)ptr);
            }
        }
        ptr = fdt_getprop(fdt, offset, "linux,initrd-end", &length);
        if (ptr) {
            if (length == 4) {
                ramdisk_end_phys = fdt32_to_cpu(*(uint32_t *)ptr);
            } else if (length == 8) {
                ramdisk_end_phys = fdt64_to_cpu(*(uint64_t *)ptr);
            }
        }

        if (ramdisk_start_phys && ramdisk_end_phys) {
            *ramdisk_base = paddr_to_kvaddr(ramdisk_start_phys);
            size_t length = ramdisk_end_phys - ramdisk_start_phys;
            *ramdisk_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        }
    }

    // look for memory size. currently only used for qemu build
    if (mem_size) {
        offset = fdt_path_offset(fdt, "/memory");
        if (offset < 0) {
            printf("%s: fdt_path_offset(/memory) failed\n", __FUNCTION__);
            return;
        }
        int lenp;
        const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
        if (prop_ptr && lenp == 0x10) {
            /* we're looking at a memory descriptor */
            //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
            *mem_size = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));
        }
    }
}

static void platform_preserve_ramdisk(void) {
    if (!ramdisk_start_phys || !ramdisk_end_phys) {
        return;
    }

    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = (ramdisk_end_phys - ramdisk_start_phys + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(ramdisk_start_phys, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
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

#if WITH_SMP
static void platform_cpu_early_init(mdi_node_ref_t* cpu_map) {
    mdi_node_ref_t  clusters;

    if (mdi_find_node(cpu_map, MDI_CPU_MAP_CLUSTERS, &clusters) != NO_ERROR) {
        panic("platform_cpu_early_init couldn't find clusters\n");
        return;
    }

    mdi_node_ref_t  cluster;

    mdi_each_child(&clusters, &cluster) {
        mdi_node_ref_t node;
        uint8_t cpu_count;

        if (mdi_find_node(&cluster, MDI_CPU_MAP_CLUSTERS_CPU_COUNT, &node) != NO_ERROR) {
            panic("platform_cpu_early_init couldn't find cluster cpu-count\n");
            return;
        }
        if (mdi_node_uint8(&node, &cpu_count) != NO_ERROR) {
            panic("platform_cpu_early_init could not read cluster id\n");
            return;
        }

        if (cpu_cluster_count >= SMP_CPU_MAX_CLUSTERS) {
            panic("platform_cpu_early_init: MDI contains more than SMP_CPU_MAX_CLUSTERS clusters\n");
            return;
        }
        cpu_cluster_cpus[cpu_cluster_count++] = cpu_count;
    }
    arch_init_cpu_map(cpu_cluster_count, cpu_cluster_cpus);
}

static void platform_start_cpu(uint cluster, uint cpu) {
#if BCM2837
    uintptr_t sec_entry = (uintptr_t)(&arm_reset - KERNEL_ASPACE_BASE);
    unsigned long long *spin_table = (void *)(KERNEL_ASPACE_BASE + 0xd8);

    spin_table[cpu] = sec_entry;
    __asm__ __volatile__ ("" : : : "memory");
    arch_clean_cache_range(0xffff000000000000,256);     // clean out all the VC bootstrap area
    __asm__ __volatile__("sev");                        //  where the entry vectors live.
#else
    psci_cpu_on(cluster, cpu, MEMBASE + KERNEL_LOAD_OFFSET);
#endif
}

static void* allocate_one_stack(void) {
    char* stack =
        pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, NULL, NULL);
    return stack + ARCH_DEFAULT_STACK_SIZE;
}

static void platform_cpu_init(void) {
    for (uint cluster = 0; cluster < cpu_cluster_count; cluster++) {
        for (uint cpu = 0; cpu < cpu_cluster_cpus[cluster]; cpu++) {
            if (cluster != 0 || cpu != 0) {
                void* sp = allocate_one_stack();
                void* unsafe_sp = NULL;
#if __has_feature(safe_stack)
                unsafe_sp = allocate_one_stack();
#endif
                arm64_set_secondary_sp(cluster, cpu, sp, unsafe_sp);
                platform_start_cpu(cluster, cpu);
            }
        }
    }
}
#endif // WITH_SMP

static inline bool is_bootdata_container(void* addr) {
    DEBUG_ASSERT(addr);

    bootdata_t* header = (bootdata_t*)addr;

    return header->type == BOOTDATA_CONTAINER;
}

static void ramdisk_from_bootdata_container(void* bootdata,
                                            void** ramdisk_base,
                                            size_t* ramdisk_size) {
    bootdata_t* header = (bootdata_t*)bootdata;

    DEBUG_ASSERT(header->type == BOOTDATA_CONTAINER);

    *ramdisk_base = (void*)bootdata;
    *ramdisk_size = ROUNDUP(header->length + sizeof(*header), PAGE_SIZE);
}

static void platform_mdi_init(void) {
    mdi_node_ref_t  root;
    mdi_node_ref_t  cpu_map;
    mdi_node_ref_t  kernel_drivers;

    // Look for MDI data in ramdisk bootdata
    size_t offset = 0;
    bootdata_t* header = (ramdisk_base + offset);
    if (header->type != BOOTDATA_CONTAINER) {
        panic("invalid bootdata container header\n");
    }
    offset += sizeof(*header);

    while (offset < ramdisk_size) {
        header = (ramdisk_base + offset);

        if (header->type == BOOTDATA_MDI) {
            break;
        } else {
            offset += BOOTDATA_ALIGN(sizeof(*header) + header->length);
        }
    }
    if (offset >= ramdisk_size) {
        panic("No MDI found in ramdisk\n");
    }

    if (mdi_init(ramdisk_base + offset, ramdisk_size - offset, &root) != NO_ERROR) {
        panic("mdi_init failed\n");
    }

    // search top level nodes for CPU info and kernel drivers
    if (mdi_find_node(&root, MDI_CPU_MAP, &cpu_map) != NO_ERROR) {
        panic("platform_mdi_init couldn't find cpu-map\n");
    }
    if (mdi_find_node(&root, MDI_KERNEL_DRIVERS, &kernel_drivers) != NO_ERROR) {
        panic("platform_mdi_init couldn't find kernel-drivers\n");
    }

#if WITH_SMP
    platform_cpu_early_init(&cpu_map);
#endif

    pdev_init(&kernel_drivers);
}

void platform_early_init(void)
{
    // QEMU does not put device tree pointer in the boot-time x2 register,
    // so set it here before calling read_device_tree.
    if (boot_structure_paddr == 0) {
        boot_structure_paddr = MEMBASE;
    }

    void* boot_structure_kvaddr = paddr_to_kvaddr(boot_structure_paddr);
    if (!boot_structure_kvaddr) {
        panic("no bootdata structure!\n");
    }

    // The previous environment passes us a boot structure. It may be a
    // device tree or a bootdata container. We attempt to detect the type of the
    // container and handle it appropriately.
    size_t arena_size = 0;
    if (is_bootdata_container(boot_structure_kvaddr)) {
        // We leave out arena size for now
        ramdisk_from_bootdata_container(boot_structure_kvaddr, &ramdisk_base,
                                        &ramdisk_size);
    } else {
        // on qemu we read arena size from the device tree
        read_device_tree(&ramdisk_base, &ramdisk_size, &arena_size);
    }

    if (!ramdisk_base || !ramdisk_size) {
        panic("no ramdisk!\n");
    }

    platform_mdi_init();

    /* add the main memory arena */
    if (arena_size) {
        arena.size = arena_size;
    }
    pmm_add_arena(&arena);

#ifdef BOOTLOADER_RESERVE_START
    /* Allocate memory regions reserved by bootloaders for other functions */
    pmm_alloc_range(BOOTLOADER_RESERVE_START, BOOTLOADER_RESERVE_SIZE / PAGE_SIZE, NULL);
#endif

    platform_preserve_ramdisk();
}

void platform_init(void)
{
#if WITH_SMP
    platform_cpu_init();
#endif
}

void platform_dputs(const char* str, size_t len)
{
    while (len-- > 0) {
        char c = *str++;
        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc(c);
    }
}

int platform_dgetc(char *c, bool wait)
{
    int ret = uart_getc(wait);
    if (ret == -1)
        return -1;
    *c = ret;
    return 0;
}

void platform_pputc(char c)
{
    uart_pputc(c);
}

int platform_pgetc(char *c, bool wait)
{
     int r = uart_pgetc();
     if (r == -1) {
         return -1;
     }

     *c = r;
     return 0;
}

/* stub out the hardware rng entropy generator, which doesn't exist on this platform */
size_t hw_rng_get_entropy(void* buf, size_t len, bool block) {
    return 0;
}

/* no built in framebuffer */
status_t display_get_info(struct display_info *info) {
    return ERR_NOT_FOUND;
}

void platform_halt(platform_halt_action suggested_action, platform_halt_reason reason)
{

    if (suggested_action == HALT_ACTION_REBOOT) {

#if BCM2837
#define PM_PASSWORD 0x5a000000
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020
        *REG32(PM_WDOG) =  PM_PASSWORD | 1; // timeout = 1/16th of a second? (whatever)
        *REG32(PM_RSTC) =  PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
        while(1){
        }
#else
        psci_system_reset();
#ifdef MSM8998_PSHOLD_PHYS
        // Deassert PSHold
        *REG32(paddr_to_kvaddr(MSM8998_PSHOLD_PHYS)) = 0;
#endif
#endif
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        // XXX shutdown seem to not work through psci
        // implement shutdown via pmic
#if BCM2837
        printf("shutdown is unsupported\n");
#else
        psci_system_off();
#endif
    }

#if WITH_LIB_DEBUGLOG
#if WITH_PANIC_BACKTRACE
    thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
#endif
    dlog_bluescreen_halt();
#endif

#if ENABLE_PANIC_SHELL
    if (reason == HALT_REASON_SW_PANIC) {
        dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %u)\n", reason);
        arch_disable_ints();
        panic_shell_start();
    }
#endif  // ENABLE_PANIC_SHELL

    dprintf(ALWAYS, "HALT: spinning forever... (reason = %u)\n", reason);

    // catch all fallthrough cases
    arch_disable_ints();
    for (;;);
}
