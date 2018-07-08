// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "boot-shim.h"
#include "debug.h"
#include "devicetree.h"
#include "util.h"

#include <ddk/protocol/platform-defs.h>
#include <libzbi/zbi.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <zircon/boot/driver-config.h>

// uncomment to dump device tree at boot
// #define PRINT_DEVICE_TREE

// Uncomment to list ZBI items.
#ifndef PRINT_ZBI
#define PRINT_ZBI 0
#endif

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

// Parse the device tree to find our ZBI, kernel command line, and RAM size.
static void* read_device_tree(void* device_tree, device_tree_context_t* ctx) {
    ctx->node = NODE_NONE;
    ctx->initrd_start = 0;
    ctx->memory_base = 0;
    ctx->memory_size = 0;
    ctx->cmdline = NULL;
    ctx->cpu_count = 0;

    devicetree_t dt;
    dt.error = uart_puts;
    int ret = dt_init(&dt, device_tree, 0xffffffff);
    if (ret) {
        fail("dt_init failed\n");
    }
    dt_walk(&dt, node_callback, prop_callback, ctx);

#if USE_DEVICE_TREE_CPU_COUNT
    set_cpu_count(ctx->cpu_count);
#endif

    // Use the device tree initrd as the ZBI.
    return (void*)ctx->initrd_start;
}

static void append_from_device_tree(zbi_header_t* zbi,
                                    device_tree_context_t* ctx) {
    // look for optional RAM size in device tree
    // do this last so device tree can override value in boot-shim-config.h
    if (ctx->memory_size) {
        zbi_mem_range_t mem_range;
        mem_range.paddr = ctx->memory_base;
        mem_range.length = ctx->memory_size;
        mem_range.type = ZBI_MEM_RANGE_RAM;

        uart_puts("Setting RAM base and size device tree value: ");
        uart_print_hex(ctx->memory_base);
        uart_puts(" ");
        uart_print_hex(ctx->memory_size);
        uart_puts("\n");
        append_boot_item(zbi, ZBI_TYPE_MEM_CONFIG, 0,
                         &mem_range, sizeof(mem_range));
    } else {
        uart_puts("RAM size not found in device tree\n");
    }

    // append kernel command line
    if (ctx->cmdline && ctx->cmdline_length) {
        append_boot_item(zbi, ZBI_TYPE_CMDLINE, 0,
                         ctx->cmdline, ctx->cmdline_length);
    }
}

#else

typedef struct {} device_tree_context_t;
static void* read_device_tree(void* device_tree, device_tree_context_t* ctx) {
    return NULL;
}
static void append_from_device_tree(zbi_header_t* zbi,
                                    device_tree_context_t* ctx) {
}

#endif // HAS_DEVICE_TREE

static void dump_words(const char* what, const void* data) {
    uart_puts(what);
    const uint64_t* words = data;
    for (int i = 0; i < 8; ++i) {
        uart_puts(i == 4 ? "\n       " : " ");
        uart_print_hex(words[i]);
    }
    uart_puts("\n");
}

static zbi_result_t list_zbi_cb(zbi_header_t* item, void* payload, void* ctx) {
    uart_print_hex((uintptr_t)item);
    uart_puts(": length=0x");
    uart_print_hex(item->length);
    uart_puts(" type=0x");
    uart_print_hex(item->type);
    uart_puts(" extra=0x");
    uart_print_hex(item->extra);
    uart_puts("\n");
    return ZBI_RESULT_OK;
}

static void list_zbi(zbi_header_t* zbi) {
    uart_puts("ZBI container length 0x");
    uart_print_hex(zbi->length);
    uart_puts("\n");
    zbi_for_each(zbi, &list_zbi_cb, NULL);
    uart_puts("ZBI container ends 0x");
    uart_print_hex((uintptr_t)(zbi + 1) + zbi->length);
    uart_puts("\n");
}

boot_shim_return_t boot_shim(void* device_tree) {
    uart_puts("boot_shim: hi there!\n");

    zircon_kernel_t* kernel = NULL;

    // Check the ZBI from device tree.
    device_tree_context_t ctx;
    zbi_header_t* zbi = read_device_tree(device_tree, &ctx);
    if (zbi != NULL) {
        zbi_header_t* bad_hdr;
        zbi_result_t check = zbi_check(zbi, &bad_hdr);
        if (check != ZBI_RESULT_OK) {
            fail("invalid ZBI from device tree\n");
        }
        // There is a ZBI.  Is it complete?
        if (zbi->length > sizeof(zbi_header_t) &&
            zbi[1].type == ZBI_TYPE_KERNEL_ARM64) {
            kernel = (zircon_kernel_t*) zbi;
        }
    }

    // If there is a complete ZBI from device tree, ignore whatever might
    // have been appended to the shim image.  If not, the kernel is appended.
    if (kernel == NULL) {
        zbi_header_t* bad_hdr;
        zbi_result_t check = zbi_check(&embedded_zbi, &bad_hdr);
        if (check != ZBI_RESULT_OK) {
            fail("no ZBI from device tree and no valid ZBI embedded\n");
        }
        if (embedded_zbi.hdr_file.length > sizeof(zbi_header_t) &&
            embedded_zbi.hdr_kernel.type == ZBI_TYPE_KERNEL_ARM64) {
            kernel = &embedded_zbi;
        } else {
            fail("no ARM64 kernel in ZBI from device tree or embedded ZBI\n");
        }
    }

    // If there was no ZBI at all from device tree then use the embedded ZBI
    // along with the embedded kernel.  Otherwise always use the ZBI from
    // device tree, whether the kernel is in that ZBI or was embedded.
    if (zbi == NULL) {
        zbi = &kernel->hdr_file;
    }

    // Add board-specific ZBI items.
    append_board_boot_item(zbi);

    // Append items from device tree.
    append_from_device_tree(zbi, &ctx);

    uint8_t* const kernel_end =
        (uint8_t*)&kernel->data_kernel +
        kernel->hdr_kernel.length +
        kernel->data_kernel.reserve_memory_size;

    uart_puts("Kernel at ");
    uart_print_hex((uintptr_t)kernel);
    uart_puts(" to ");
    uart_print_hex((uintptr_t)kernel_end);
    uart_puts(" reserved ");
    uart_print_hex(kernel->data_kernel.reserve_memory_size);
    uart_puts("\nZBI at ");
    uart_print_hex((uintptr_t)zbi);
    uart_puts(" to ");
    uart_print_hex((uintptr_t)(zbi + 1) + zbi->length);
    uart_puts("\n");

    if ((uint8_t*)zbi < kernel_end && zbi != &kernel->hdr_file) {
        fail("expected kernel to be loaded lower in memory than initrd\n");
    }

    if (PRINT_ZBI) {
        list_zbi(zbi);
    }

    if (zbi == &kernel->hdr_file || (uintptr_t)zbi % 4096 != 0) {
        // The ZBI needs to be page-aligned, so move it up.
        // If it's a complete ZBI, splice out the kernel and move it higher.
        zbi_header_t* old = zbi;
        zbi = (void*)(((uintptr_t)old + 4095) & -(uintptr_t)4096);
        if (old == &kernel->hdr_file) {
            // Length of the kernel item payload, without header.
            uint32_t kernel_len = kernel->hdr_kernel.length;

            // Length of the ZBI container, including header, without kernel.
            uint32_t zbi_len = kernel->hdr_file.length - kernel_len;

            uart_puts("Splitting kernel ");
            uart_print_hex(kernel_len);
            uart_puts(" from ZBI ");
            uart_print_hex(zbi_len);

            // First move the kernel up out of the way.
            uintptr_t zbi_end = (uintptr_t)(old + 1) + old->length;
            if (zbi_end < (uintptr_t)zbi + zbi_len) {
                zbi_end =  (uintptr_t)zbi + zbi_len;
            }
            kernel = (void*)((zbi_end + KERNEL_ALIGN - 1) &
                             -(uintptr_t)KERNEL_ALIGN);
            uart_puts("\nKernel to ");
            uart_print_hex((uintptr_t)kernel);
            memcpy(kernel, old, (2 * sizeof(*zbi)) + kernel_len);
            // Fix up the kernel's solo container size.
            kernel->hdr_file.length = sizeof(*zbi) + kernel_len;

            // Now move the ZBI into its aligned place and fix up the
            // container header to exclude the kernel.
            uart_puts(" ZBI to ");
            uart_print_hex((uintptr_t)zbi);
            zbi_header_t header = *old;
            header.length -= kernel->hdr_file.length;
            void* payload = (uint8_t*)(old + 1) + kernel->hdr_file.length;
            memmove(zbi + 1, payload, header.length);
            *zbi = header;

            uart_puts("\nKernel container length ");
            uart_print_hex(kernel->hdr_file.length);
            uart_puts(" ZBI container length ");
            uart_print_hex(zbi->length);
            uart_puts("\n");
        } else {
            uart_puts("Relocating whole ZBI for alignment\n");
            memmove(zbi, old, sizeof(*old) + old->length);
        }
    }

    if ((uintptr_t)kernel % KERNEL_ALIGN != 0) {
        // The kernel has to be relocated for alignment.
        uart_puts("Relocating kernel for alignment\n");
        zbi_header_t* old = &kernel->hdr_file;
        kernel = (void*)(((uintptr_t)(zbi + 1) + zbi->length +
                          KERNEL_ALIGN - 1) & -(uintptr_t)KERNEL_ALIGN);
        memmove(kernel, old, sizeof(*old) + old->length);
    }

    boot_shim_return_t result = {
        .entry = (uintptr_t)kernel + kernel->data_kernel.entry,
        .zbi = zbi,
    };
    uart_puts("Entering kernel at ");
    uart_print_hex(result.entry);
    uart_puts(" with ZBI ");
    uart_print_hex((uintptr_t)result.zbi);
    uart_puts("\n");
    return result;
}
