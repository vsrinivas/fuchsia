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
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>

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

//static uint8_t * kernel_args;

static pmm_arena_info_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

void platform_init_mmu_mappings(void)
{
}

#if FASTBOOT_HEADER
extern ulong lk_boot_args[4];

// find our command line in the device tree (fastboot stuffs it in there)
static void find_command_line(void) {
    void* fdt = paddr_to_kvaddr(lk_boot_args[0]);
    if (!fdt) {
        printf("msm8998: could not find device tree\n");
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("msm8998: fdt_check_header failed\n");
        return;
    }

    int depth = 0;
    int offset = 0;
    for (;;) {
        offset = fdt_next_node(fdt, offset, &depth);
        if (offset < 0)
            break;

        const char* name = fdt_get_name(fdt, offset, NULL);
        if (!name)
            continue;
        if (strcmp(name, "chosen") == 0) {
            int lenp;
            const char* bootargs = fdt_getprop(fdt, offset, "bootargs", &lenp);
            if (bootargs) {
                printf("msm8998 command line: %s\n", bootargs);
                cmdline_init(bootargs);
                return;
            }
        }
    }
}
#endif // FASTBOOT_HEADER

void platform_early_init(void)
{
#if FASTBOOT_HEADER
    find_command_line();
#endif

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

    psci_cpu_on(0,1, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(0,2, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(0,3, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(1,0, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(1,1, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(1,2, MEMBASE + KERNEL_LOAD_OFFSET);
    psci_cpu_on(1,3, MEMBASE + KERNEL_LOAD_OFFSET);
}

void platform_init(void)
{
    uart_init();
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

/* Default implementation of panic time getc/putc.
 * Just calls through to the underlying dputc/dgetc implementation
 * unless the platform overrides it.
 */
__WEAK void platform_pputc(char c)
{
    return platform_dputc(c);
}

__WEAK int platform_pgetc(char *c, bool wait)
{
    return platform_dgetc(c, wait);
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
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        // XXX shutdown seem to not work through psci
        // implement shutdown via pmic
#if 0
        psci_system_off();
#endif
        printf("shutdown is unsupported\n");
    }

    // Deassert PSHold
    *REG32(MSM8998_PSHOLD_VIRT) = 0;

    // catch all fallthrough cases
    arch_disable_ints();
    for (;;);
}
