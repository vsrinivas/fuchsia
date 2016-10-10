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
 { 0 }
};

#define DEBUG_UART 1

extern void intc_init(void);

extern void arm_reset(void);

//static fb_mbox_t framebuff_descriptor __ALIGNED(64);

//static uint8_t * vbuff;

static uint8_t * kernel_args;

static pmm_arena_t arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = MEMSIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

void platform_init_mmu_mappings(void)
{
}

/* empty call to satisfy current pcie dependencies in Magenta
 *  TODO - remove once pcie dependencies are resolved (see bug MG-246)
 */
#include <dev/pcie.h>
void platform_pcie_init_info(pcie_init_info_t *out)
{

}

void platform_early_init(void)
{
    atag_t * tag;

    tag = atag_find(RPI_ATAG_ATAG_CMDLINE, RPI_ATAGS_ADDRESS);
    if (tag) {
        kernel_args = tag->cmdline;
    }

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

                    /* trim size on certain platforms */
#if ARCH_ARM
                    if (len > 1024*1024*1024U) {
                        len = 1024*1024*1024; /* only use the first 1GB on ARM32 */
                        //printf("trimming memory to 1GB\n");
                    }
#endif

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

    /* Get framebuffer for jraphics */

    // framebuff_descriptor.phys_width  = 960;
    // framebuff_descriptor.phys_height = 540;
    // framebuff_descriptor.virt_width  = 960;
    // framebuff_descriptor.virt_height = 540;
    // framebuff_descriptor.pitch       = 0;
    // framebuff_descriptor.depth       = 32;
    // framebuff_descriptor.virt_x_offs = 0;
    // framebuff_descriptor.virt_y_offs = 0;
    // framebuff_descriptor.fb_p        = 0;
    // framebuff_descriptor.fb_size     = 0;

    // if (!get_vcore_framebuffer(&framebuff_descriptor)) {
    //     printf ("fb returned at 0x%08x of %d bytes in size\n",framebuff_descriptor.fb_p
    //                                                          ,framebuff_descriptor.fb_size);

    //     vbuff = (uint8_t *)((framebuff_descriptor.fb_p & 0x3fffffff) + KERNEL_BASE);
    //     printf("video buffer at %lx\n",(uintptr_t )vbuff);
    //     printf("pitch: %d\n",framebuff_descriptor.pitch);
    // }
}
void arm64_get_cache_levels(uint32_t * levels);
void arm64_get_cache_linesize(uint32_t * linesize);

void target_init(void)
{

    //uint32_t * temp;

    //uint32_t addr;

    // temp = (uint32_t *)get_vcore_single(0x00010005,8,8);
    // if (temp) printf ("ARM memory base:0x%08x len:0x%08x\n",temp[0],temp[1]);

    // temp = (uint32_t *)get_vcore_single(0x00010006,8,8);
    // if (temp) printf ("VC  memory base:0x%08x len:0x%08x\n",temp[0],temp[1]);

    // temp = (uint32_t *)get_vcore_single(0x00010004,8,8);
    // if (temp) printf ("SERIAL # %08x%08x\n",temp[1],temp[0]);

    // arm64_get_cache_levels(temp);
    // printf("Cache Levels= 0x%08x\n",*temp);

    // arm64_get_cache_linesize(temp);
    // printf("Cache Line Size= 0x%08x\n",*temp);

}

static void flush(void){
    //arch_clean_cache_range(vbuff,framebuff_descriptor.fb_size);
}

// status_t display_get_framebuffer(struct display_framebuffer *fb)
// {
//     fb->image.pixels = (void *)vbuff;

//     fb->format = DISPLAY_FORMAT_ARGB_8888;
//     fb->image.format = IMAGE_FORMAT_ARGB_8888;
//     fb->image.rowbytes = framebuff_descriptor.phys_width * 4;

//     fb->image.width = framebuff_descriptor.phys_width;
//     fb->image.height = framebuff_descriptor.phys_height;
//     fb->image.stride = framebuff_descriptor.phys_width;
//     fb->flush = flush;

//     return NO_ERROR;
// }

// status_t display_get_info(struct display_info *info)
// {
//     info->format = DISPLAY_FORMAT_ARGB_8888;
//     info->width = framebuff_descriptor.phys_width;
//     info->height = framebuff_descriptor.phys_height;

//     return NO_ERROR;
// }

// status_t display_present(struct display_image *image, uint starty, uint endy)
// {
//   TRACEF("display_present - not implemented");
//   DEBUG_ASSERT(false);
//   return NO_ERROR;
// }


void platform_dputc(char c)
{
    if (c == '\n')
        uart_putc(DEBUG_UART, '\r');
    uart_putc(DEBUG_UART, c);
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


