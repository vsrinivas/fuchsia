// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trampoline.h"

#include <inttypes.h>
#include <libzbi/zbi.h>
#include <string.h>
#include <zircon/boot/e820.h>

#define BOOT_LOADER_NAME_ENV "multiboot.boot_loader_name="

static size_t zbi_size(const zbi_header_t* zbi) {
    return sizeof(*zbi) + zbi->length;
}

// Convert the multiboot memory information to ZBI_TYPE_E820_TABLE format.
static void add_memory_info(void* zbi, size_t capacity,
                            const multiboot_info_t* info) {
    if ((info->flags & MB_INFO_MMAP) &&
        info->mmap_addr != 0 &&
        info->mmap_length >= sizeof(memory_map_t)) {
        size_t nranges = 0;
        for (memory_map_t* mmap = (void*)info->mmap_addr;
             (uintptr_t)mmap < info->mmap_addr + info->mmap_length;
             mmap = (void*)((uintptr_t)mmap + 4 + mmap->size)) {
            ++nranges;
        }
        e820entry_t* ranges;
        void* payload;
        zbi_result_t result = zbi_create_section(
            zbi, capacity, nranges * sizeof(ranges[0]),
            ZBI_TYPE_E820_TABLE, 0, 0, &payload);
        if (result != ZBI_RESULT_OK) {
            panic("zbi_create_section(%p, %#"PRIxPTR", %#zx) failed: %d",
                  zbi, capacity, nranges * sizeof(ranges[0]), (int)result);
        }
        ranges = payload;
        for (memory_map_t* mmap = (void*)info->mmap_addr;
             (uintptr_t)mmap < info->mmap_addr + info->mmap_length;
             mmap = (void*)((uintptr_t)mmap + 4 + mmap->size)) {
            *ranges++ = (e820entry_t){
                .addr = (((uint64_t)mmap->base_addr_high << 32) |
                         mmap->base_addr_low),
                .size = (((uint64_t)mmap->length_high << 32) |
                         mmap->length_low),
                // MB_MMAP_TYPE_* matches E820_* values.
                .type = mmap->type,
            };
        }
    } else {
        const e820entry_t ranges[] = {
            {
                .addr = 0,
                .size = info->mem_lower << 10,
                .type = E820_RAM,
            },
            {
                .addr = 1 << 20,
                .size = ((uint64_t)info->mem_upper << 10) - (1 << 20),
                .type = E820_RAM,
            },
        };
        zbi_result_t result = zbi_append_section(
            zbi, capacity, sizeof(ranges), ZBI_TYPE_E820_TABLE, 0, 0, ranges);
        if (result != ZBI_RESULT_OK) {
            panic("zbi_append_section(%p, %#"PRIxPTR", %#zx) failed: %d",
                  zbi, capacity, sizeof(ranges), (int)result);
        }
    }
}

static void add_cmdline(void* zbi, size_t capacity,
                            const multiboot_info_t* info) {
    // Boot loader command line.
    if (info->flags & MB_INFO_CMD_LINE) {
        const char* cmdline = (void*)info->cmdline;
        size_t len = strlen(cmdline) + 1;
        zbi_result_t result = zbi_append_section(
            zbi, capacity, len, ZBI_TYPE_CMDLINE, 0, 0, cmdline);
        if (result != ZBI_RESULT_OK) {
            panic("zbi_append_section(%p, %#"PRIxPTR", %zu) failed: %d",
                  zbi, capacity, len, (int)result);
        }
    }

    // Boot loader name.
    if (info->flags & MB_INFO_BOOT_LOADER) {
        const char* name = (void*)info->boot_loader_name;
        size_t len = strlen(name) + 1;
        void *payload;
        zbi_result_t result = zbi_create_section(
            zbi, capacity, sizeof(BOOT_LOADER_NAME_ENV) - 1 + len,
            ZBI_TYPE_CMDLINE, 0, 0, &payload);
        if (result != ZBI_RESULT_OK) {
            panic("zbi_create_section(%p, %#"PRIxPTR", %zu) failed: %d",
                  zbi, capacity, sizeof(BOOT_LOADER_NAME_ENV) - 1 + len,
                  (int)result);
        }
        for (char *p = (memcpy(payload, BOOT_LOADER_NAME_ENV,
                               sizeof(BOOT_LOADER_NAME_ENV) - 1) +
                        sizeof(BOOT_LOADER_NAME_ENV) - 1);
             len > 0;
             ++p, ++name, --len) {
            *p = (*name == ' ' || *name == '\t' ||
                  *name == '\n' || *name == '\r') ? '+' : *name;
        }
    }
}

static void add_zbi_items(void* zbi, size_t capacity,
                            const multiboot_info_t* info) {
    add_memory_info(zbi, capacity, info);
    add_cmdline(zbi, capacity, info);
}

noreturn void multiboot_main(uint32_t magic, multiboot_info_t* info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("bad multiboot magic from bootloader %#"PRIx32" != %#"PRIx32"\n",
              magic, (uint32_t)MULTIBOOT_BOOTLOADER_MAGIC);
    }

    if (!(info->flags & MB_INFO_MODS)) {
        panic("missing multiboot modules");
    }
    if (info->mods_count != 1) {
        panic("cannot handle multiboot mods_count %"PRIu32" != 1\n",
              info->mods_count);
    }

    module_t* const mod = (void*)info->mods_addr;
    zircon_kernel_t* zbi = (void*)mod->mod_start;
    size_t zbi_len = mod->mod_end - mod->mod_start;

    if (zbi == NULL || zbi_len < sizeof(zbi->hdr_file) ||
        zbi_len < sizeof(zbi_header_t) + zbi->hdr_file.length) {
        panic("insufficient multiboot module [%#"PRIx32",%#"PRIx32") for ZBI",
              mod->mod_start, mod->mod_end);
    }

    zbi_header_t* bad_hdr;
    zbi_result_t result = zbi_check(zbi, &bad_hdr);
    if (result != ZBI_RESULT_OK) {
        panic("ZBI failed check: %d at offset %#zx",
              (int)result, (size_t)((uint8_t*)bad_hdr - (uint8_t*)zbi));
    }

    if (zbi->hdr_kernel.type != ZBI_TYPE_KERNEL_X64) {
        panic("ZBI first item has type %#"PRIx32" != %#"PRIx32"\n",
              zbi->hdr_kernel.type, (uint32_t)ZBI_TYPE_KERNEL_X64);
    }

    // The kernel will sit at PHYS_LOAD_ADDRESS, where the code now
    // running sits.  The space until kernel_memory_end is reserved
    // and can't be used for anything else.
    const size_t kernel_load_size =
        offsetof(zircon_kernel_t, data_kernel) + zbi->hdr_kernel.length;
    uint8_t* const kernel_load_end = PHYS_LOAD_ADDRESS + kernel_load_size;
    uint8_t* const kernel_memory_end =
        kernel_load_end + ZBI_ALIGN(zbi->data_kernel.reserve_memory_size);

    if (!(info->flags & MB_INFO_MEM_SIZE)) {
        panic("multiboot memory information missing");
    }
    uintptr_t upper_memory_limit = info->mem_upper << 10;
    if (info->mem_upper > (UINT32_MAX >> 10)) {
        upper_memory_limit = -4096u;
    }
    if (upper_memory_limit < (uintptr_t)kernel_memory_end) {
        panic("upper memory limit %#"PRIxPTR" < kernel end %p",
              upper_memory_limit, kernel_memory_end);
    }

    // Now we can append other items to the ZBI.
    const size_t capacity = upper_memory_limit - (uintptr_t)zbi;
    add_zbi_items(zbi, capacity, info);

    // Use discarded ZBI space to hold the trampoline.
    void* trampoline;
    result = zbi_create_section(zbi, capacity, sizeof(struct trampoline),
                                ZBI_TYPE_DISCARD, 0, 0, &trampoline);
    if (result != ZBI_RESULT_OK) {
        panic("zbi_create_section(%p, %#"PRIxPTR", %#zx) failed: %d",
              zbi, capacity, sizeof(struct trampoline), (int)result);
    }

    // Separate the kernel from the data ZBI and move it into temporary
    // space that the trampoline code will copy it from.
    const zbi_header_t data_zbi_hdr = ZBI_CONTAINER_HEADER(
        zbi->hdr_file.length -
        sizeof(zbi->hdr_kernel) -
        zbi->hdr_kernel.length);
    uint8_t* const zbi_end = (uint8_t*)zbi + zbi_size(&zbi->hdr_file);
    zircon_kernel_t* kernel;
    zbi_header_t* data;
    uintptr_t free_memory;
    if ((uint8_t*)zbi < kernel_memory_end) {
        // The ZBI overlaps where the kernel needs to sit.  Copy it further up.
        if (zbi_end < kernel_memory_end) {
            data = (void*)kernel_memory_end;
        } else {
            data = (void*)zbi_end;
        }
        // It needs to be page-aligned.
        data = (void*)(((uintptr_t)data + 4095) & -4096);
        // First fill in the data ZBI.
        *data = data_zbi_hdr;
        memcpy(data + 1,
               (uint8_t*)&zbi->data_kernel + zbi->hdr_kernel.length,
               data->length);
        // Now place the kernel after that.
        kernel = (void*)((uint8_t*)(data + 1) + data->length);
        memcpy(kernel, zbi, kernel_load_size);
        free_memory = (uintptr_t)kernel + kernel_load_size;
    } else {
        // The ZBI can stay where it is.  Find a place to copy the kernel.
        size_t space = (uint8_t*)zbi - kernel_memory_end;
        if (space >= kernel_load_size) {
            // There's space right after the final kernel location.
            kernel = (void*)kernel_memory_end;
            free_memory = (uintptr_t)zbi_end;
        } else {
            // Use space after the ZBI.
            kernel = (void*)((uint8_t*)zbi + zbi_size(&zbi->hdr_file));
            free_memory = (uintptr_t)kernel + kernel_load_size;
        }
        // Copy the kernel into some available scratch space.
        memcpy(kernel, zbi, kernel_load_size);
        // Now clobber what was the tail end of the kernel with a new
        // container header for just the remainder of the ZBI.
        data = (void*)((uint8_t*)&zbi->data_kernel + zbi->hdr_kernel.length);
        *data = data_zbi_hdr;
        // Move the ZBI up to get it page-aligned.
        if ((uintptr_t)data % 4096 != 0) {
            void* orig = data;
            data = (void*)(((uintptr_t)data + 4095) & -4096);
            memmove(data, orig, zbi_size(data));
        }
    }

    // Fix up the kernel container's size.
    kernel->hdr_file.length =
        sizeof(kernel->hdr_kernel) + kernel->hdr_kernel.length;

    // Set up page tables in free memory.
    enable_64bit_paging(free_memory, upper_memory_limit);

    // Copy the kernel into place and enter its code in 64-bit mode.
    boot_zbi(kernel, data, trampoline);
}
