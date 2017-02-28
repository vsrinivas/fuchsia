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
#include <dev/timer/arm_generic.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/psci.h>

#include <platform.h>
#include <dev/interrupt.h>
#include <dev/interrupt/arm_gic.h>
#include <platform/msm8998.h>

#include <target.h>

#include <libfdt.h>
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

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
 /* 1GB of sdram space */
 {
     .phys = SDRAM_BASE,
     .virt = KERNEL_BASE,
     .size = MEMORY_APERTURE_SIZE,
     .flags = 0,
     .name = "memory"
 },

 /* peripherals */
 {
     .phys = MSM8998_PERIPH_BASE_PHYS,
     .virt = MSM8998_PERIPH_BASE_VIRT,
     .size = MSM8998_PERIPH_SIZE,
     .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
     .name = "msm peripherals"
 },
 /* null entry to terminate the list */
 {}
};

#define DEBUG_UART 1

extern void arm_reset(void);

static pmm_arena_info_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
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

void platform_init_mmu_mappings(void)
{
}

static uint64_t bootloader_ramdisk_base;
static uint64_t bootloader_ramdisk_size;
static void* ramdisk_base;
static size_t ramdisk_size;

static void platform_preserve_ramdisk(void) {
    if (bootloader_ramdisk_size == 0) {
        return;
    }
    if (bootloader_ramdisk_base == 0) {
        return;
    }
    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = (bootloader_ramdisk_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(bootloader_ramdisk_base, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }

    ramdisk_base = paddr_to_kvaddr(bootloader_ramdisk_base);
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

extern ulong lk_boot_args[4];

// find our command line and ramdisk in the device tree
static void read_device_tree(void) {
    void* fdt = paddr_to_kvaddr(lk_boot_args[0]);
    if (!fdt) {
        printf("msm8998: could not find device tree\n");
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("msm8998: fdt_check_header failed\n");
        return;
    }

    int offset = fdt_path_offset(fdt, "/chosen");
    if (offset < 0) {
        printf("msm8998: fdt_path_offset(/chosen) failed\n");
        return;
    }

    int length;
    const char* bootargs = fdt_getprop(fdt, offset, "bootargs", &length);
    if (bootargs) {
        cmdline_init(bootargs);
    }

    const uint64_t* ramdisk_start_ptr = fdt_getprop(fdt, offset, "linux,initrd-start", &length);
    const uint64_t* ramdisk_end_ptr = fdt_getprop(fdt, offset, "linux,initrd-end", &length);
    if (ramdisk_start_ptr && ramdisk_end_ptr) {
        bootloader_ramdisk_base = fdt64_to_cpu(*ramdisk_start_ptr);
        bootloader_ramdisk_size = fdt64_to_cpu(*ramdisk_end_ptr) - bootloader_ramdisk_base;
    }
}

void platform_early_init(void)
{
    read_device_tree();

    uart_init_early();

    /* initialize the interrupt controller and timers */
    arm_gic_init();
    arm_generic_timer_init(ARM_GENERIC_TIMER_PHYSICAL_VIRT, 0);

    /* add the main memory arena */
    pmm_add_arena(&arena);

    /* Allocate memory regions reserved by bootloaders for other functions */
    struct list_node list = LIST_INITIAL_VALUE(list);
    pmm_alloc_range(MSM8998_BOOT_HYP_START,
                    (MSM8998_BOOT_APSS2_START - MSM8998_BOOT_HYP_START)/ PAGE_SIZE,
                    &list);

    platform_preserve_ramdisk();
}

void platform_init(void)
{
    uart_init();

#if WITH_SMP
    /* TODO - number of cpus (and topology) should be parsed from device index or command line */

    for (int i = 1; i < SMP_MAX_CPUS; i++) {

        uint64_t mpid = (PSCI_INDEX_TO_CLUSTER(i) << 8) | PSCI_INDEX_TO_ID(i);

        arm64_set_secondary_sp(mpid, pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, NULL, NULL));

        psci_cpu_on( PSCI_INDEX_TO_CLUSTER(i) , PSCI_INDEX_TO_ID(i), MEMBASE + KERNEL_LOAD_OFFSET);
    }

#endif
}

void target_init(void)
{

}

void platform_dputs(const char* str, size_t len)
{
    while (len-- > 0) {
        char c = *str++;
        if (c == '\n') {
            uart_putc(DEBUG_UART, '\r');
        }
        uart_putc(DEBUG_UART, c);
    }
}

int platform_dgetc(char *c, bool wait)
{
    int ret = uart_getc(DEBUG_UART, wait);
    if (ret == -1)
        return -1;
    *c = ret;
    return 0;
}

void platform_pputc(char c)
{
    uart_pputc(DEBUG_UART, c);
}

int platform_pgetc(char *c, bool wait)
{
     int r = uart_pgetc(DEBUG_UART);
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
        psci_system_reset();
        // Deassert PSHold
        *REG32(MSM8998_PSHOLD_VIRT) = 0;
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        // XXX shutdown seem to not work through psci
        // implement shutdown via pmic
#if 0
        psci_system_off();
#endif
        printf("shutdown is unsupported\n");
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
