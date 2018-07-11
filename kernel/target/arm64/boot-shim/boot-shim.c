// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "boot-shim.h"
#include "debug.h"
#include "devicetree.h"
#include "util.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <ddk/protocol/platform-defs.h>
#include <zircon/boot/driver-config.h>
#include <zbi/zbi.h>

// uncomment to dump device tree at boot
// #define PRINT_DEVICE_TREE

// used in boot-shim-config.h and in this file below
static void append_boot_item(zbi_header_t* container,
                             uint32_t type, uint32_t extra,
                             const void* payload, uint32_t length) {
    zbi_result_t result = zbi_append_section(
        container, SIZE_MAX, length, type, extra, 0, payload);
    if (result != ZBI_RESULT_OK) {
        fail("zbi_append_section failed\n");
    }
}

// defined in boot-shim-config.h
static void append_board_boot_item(zbi_header_t* container);

#if USE_DEVICE_TREE_CPU_COUNT
static void set_cpu_count(uint32_t cpu_count);
#endif

// Include board specific definitions
#include "boot-shim-config.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

#if HAS_DEVICE_TREE
typedef enum {
    NODE_NONE,
    NODE_CHOSEN,
    NODE_MEMORY,
} node_t;

typedef struct {
    node_t  node;
    uintptr_t initrd_start;
    size_t memory_base;
    size_t memory_size;
    char* cmdline;
    size_t cmdline_length;
    uint32_t cpu_count;
} device_tree_context_t;

static int node_callback(int depth, const char *name, void *cookie) {
#ifdef PRINT_DEVICE_TREE
    uart_puts("node: ");
    uart_puts(name);
    uart_puts("\n");
#endif

    device_tree_context_t* ctx = cookie;

    if (!strcmp(name, "chosen")) {
        ctx->node = NODE_CHOSEN;
    } else if (!strcmp(name, "memory") || !strcmp(name, "memory@00000000")) {
        ctx->node = NODE_MEMORY;
    } else {
        ctx->node = NODE_NONE;
        if (!strncmp(name, "cpu@", 4)) {
            ctx->cpu_count++;
        }
    }

    return 0;
}

static int prop_callback(const char *name, uint8_t *data, uint32_t size, void *cookie) {
#ifdef PRINT_DEVICE_TREE
    uart_puts("    prop: ");
    uart_puts(name);
    uart_puts(" size: ");
    uart_print_hex(size);
    uart_puts("\n");
#endif

    device_tree_context_t* ctx = cookie;

    if (ctx->node == NODE_CHOSEN) {
        if (!strcmp(name, "linux,initrd-start")) {
            if (size == sizeof(uint32_t)) {
                ctx->initrd_start = dt_rd32(data);
            } else if (size == sizeof(uint64_t)) {
                uint64_t most = dt_rd32(data);
                uint64_t least = dt_rd32(data + 4);
                ctx->initrd_start = (most << 32) | least;
            } else {
                fail("bad size for linux,initrd-start in device tree\n");
            }
        } else if (!strcmp(name, "bootargs")) {
            ctx->cmdline = (char *)data;
            ctx->cmdline_length = size;
        }
    } else if (ctx->node == NODE_MEMORY) {
        if (!strcmp(name, "reg") && size == 16) {
            // memory size is big endian uint64_t at offset 0
            uint64_t most = dt_rd32(data + 0);
            uint64_t least = dt_rd32(data + 4);
            ctx->memory_base = (most << 32) | least;
            // memory size is big endian uint64_t at offset 8
            most = dt_rd32(data + 8);
            least = dt_rd32(data + 12);
            ctx->memory_size = (most << 32) | least;
        }
    }

    return 0;
}

static void read_device_tree(void* device_tree, device_tree_context_t* ctx) {
    devicetree_t dt;
    dt.error = uart_puts;

    int ret = dt_init(&dt, device_tree, 0xffffffff);
    if (ret) {
        fail("dt_init failed\n");
    }
    dt_walk(&dt, node_callback, prop_callback, ctx);
}
#endif // HAS_DEVICE_TREE

boot_shim_return_t boot_shim(void* device_tree) {
    uart_puts("boot_shim: hi there!\n");

    zircon_kernel_t* const kernel = &kernel_bootdata;

    // sanity check the bootdata headers
    // it must start with a container record followed by a kernel record
    if (kernel->hdr_file.type != ZBI_TYPE_CONTAINER ||
        kernel->hdr_file.extra != ZBI_CONTAINER_MAGIC ||
        kernel->hdr_file.magic != ZBI_ITEM_MAGIC ||
        // TODO(ZX-2153,gkalsi): Validate that this is specifically an ARM64 kernel
        !ZBI_IS_KERNEL_BOOTITEM(kernel->hdr_kernel.type) ||
        kernel->hdr_kernel.magic != ZBI_ITEM_MAGIC) {
        fail("zircon_kernel_t sanity check failed\n");
    }

    uint32_t bootdata_size = kernel->hdr_file.length + sizeof(zbi_header_t);
    uint32_t kernel_size = kernel->hdr_kernel.length + 2 * sizeof(zbi_header_t);

    // If we have bootdata following the kernel, then the kernel is the beginning of our bootdata.
    // Otherwise we will need to look for the bootdata in the device tree
    zbi_header_t* bootdata = (bootdata_size > kernel_size ? &kernel->hdr_file : NULL);

    // If we have more bootdata following the kernel we must relocate the kernel
    // past the end of the bootdata so the kernel bss does not collide with it.
    // We will do this relocation after we are done appending new bootdata items.
    bool relocate_kernel = (bootdata != NULL);

#if HAS_DEVICE_TREE
    // Parse the Linux device tree to find our bootdata, kernel command line and RAM size
    device_tree_context_t ctx;
    ctx.node = NODE_NONE;
    ctx.initrd_start = 0;
    ctx.memory_base = 0;
    ctx.memory_size = 0;
    ctx.cmdline = NULL;
    ctx.cpu_count = 0;
    read_device_tree(device_tree, &ctx);

#if USE_DEVICE_TREE_CPU_COUNT
    set_cpu_count(ctx.cpu_count);
#endif

    // find our bootdata first
    if (!bootdata) {
        if (ctx.initrd_start) {
            bootdata = (zbi_header_t*)ctx.initrd_start;
            if (bootdata->type != ZBI_TYPE_CONTAINER || bootdata->extra != ZBI_CONTAINER_MAGIC ||
                bootdata->magic != ZBI_ITEM_MAGIC) {
                fail("bad magic for bootdata in device tree\n");
            }
        } else {
            fail("could not find bootdata in device tree\n");
        }
    }
#endif // HAS_DEVICE_TREE

    // add board specific bootdata
    append_board_boot_item(bootdata);

#if HAS_DEVICE_TREE
    // look for optional RAM size in device tree
    // do this last so device tree can override value in boot-shim-config.h
    if (ctx.memory_size) {
        zbi_mem_range_t mem_range;
        mem_range.paddr = ctx.memory_base;
        mem_range.length = ctx.memory_size;
        mem_range.type = ZBI_MEM_RANGE_RAM;

        uart_puts("Setting RAM base and size device tree value: ");
        uart_print_hex(ctx.memory_base);
        uart_puts(" ");
        uart_print_hex(ctx.memory_size);
        uart_puts("\n");
        append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_range, sizeof(mem_range));
    } else {
        uart_puts("RAM size not found in device tree\n");
    }

    // append kernel command line
    if (ctx.cmdline && ctx.cmdline_length) {
        append_boot_item(bootdata, ZBI_TYPE_CMDLINE, 0, ctx.cmdline, ctx.cmdline_length);
    }
#endif // HAS_DEVICE_TREE

    uintptr_t kernel_base;

    if (relocate_kernel) {
        // recalculate bootdata_size after appending more bootdata records
        bootdata_size = kernel->hdr_file.length + sizeof(zbi_header_t);

        // round up to align new kernel location
        bootdata_size = ROUNDUP(bootdata_size, KERNEL_ALIGN);
        kernel_base = (uintptr_t)kernel + bootdata_size;

        memcpy((void *)kernel_base, kernel, ZBI_ALIGN(kernel_size));
    } else {
        kernel_base = (uintptr_t)kernel;
    }

    boot_shim_return_t result = {
        .bootdata = bootdata,
        .entry = kernel_base + kernel->data_kernel.entry,
    };
    return result;
}
