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

#include <platform.h>
#include <dev/interrupt.h>
#include <platform/bcm28xx.h>
#include <platform/videocore.h>
#include <platform/atag.h>

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
     .phys = BCM_PERIPH_BASE_PHYS,
     .virt = BCM_PERIPH_BASE_VIRT,
     .size = BCM_PERIPH_SIZE,
     .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
     .name = "bcm peripherals"
 },

 /* null entry to terminate the list */
 {}
};

#define DEBUG_UART 1

extern void intc_init(void);

extern void arm_reset(void);

static pmm_arena_info_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

void platform_init_mmu_mappings(void)
{
}

extern ulong lk_boot_args[4];

// find our command line in the device tree (fastboot stuffs it in there)
static void find_command_line(void) {
    void* fdt = paddr_to_kvaddr(lk_boot_args[0]);
    if (!fdt) {
        printf("Raspberry Pi3: could not find device tree\n");
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("Raspberry Pi3: fdt_check_header failed\n");
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
                printf("Raspberry Pi3 command line: %s\n", bootargs);
                cmdline_init(bootargs);
                return;
            }
        }
    }
}


void platform_early_init(void)
{

    find_command_line();
    uart_init_early();

    intc_init();

    arm_generic_timer_init(INTERRUPT_ARM_LOCAL_CNTPNSIRQ, 0);

   /* look for a flattened device tree just before the kernel */
    const void *fdt = (void *)KERNEL_BASE;
    int err = fdt_check_header(fdt);
    if (err >= 0) {
        /* walk the nodes, looking for 'memory' */
        int depth = 0;
        int offset = 0;
        for (;;) {
            offset = fdt_next_node(fdt, offset, &depth);
            if (offset < 0)
                break;

            /* get the name */
            const char *name = fdt_get_name(fdt, offset, NULL);
            if (!name)
                continue;

            /* look for the 'memory' property */
            if (strcmp(name, "memory") == 0) {
                //printf("Found memory in fdt\n");
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));

                    /* set the size in the pmm arena */
                    arena.size = len;
                }
            }
        }
    }

    /* add the main memory arena */
    pmm_add_arena(&arena);

    /* reserve the first 64k of ram, which should be holding the fdt */
    struct list_node list = LIST_INITIAL_VALUE(list);
    pmm_alloc_range(MEMBASE, 0x80000 / PAGE_SIZE, &list);

}

void platform_init(void)
{
    uart_init();
#if WITH_SMP
    /* TODO - number of cpus (and topology) should be parsed from device index or command line */

#if BCM2837

    uintptr_t sec_entry = (uintptr_t)(&arm_reset - KERNEL_ASPACE_BASE);
    unsigned long long *spin_table = (void *)(KERNEL_ASPACE_BASE + 0xd8);

    for (uint i = 1; i <= 3; i++) {

        arm64_set_secondary_sp(i, pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE , NULL, NULL));

        spin_table[i] = sec_entry;
        __asm__ __volatile__ ("" : : : "memory");
        arch_clean_cache_range(0xffff000000000000,256);     // clean out all the VC bootstrap area
        __asm__ __volatile__("sev");                        //  where the entry vectors live.
    }
#else
    /* start the other cpus */
    uintptr_t sec_entry = (uintptr_t)&arm_reset;
    sec_entry -= (KERNEL_BASE - MEMBASE);
    for (uint i = 1; i <= 3; i++) {
        *REG32(ARM_LOCAL_BASE + 0x8c + 0x10 * i) = sec_entry;
    }
#endif
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
