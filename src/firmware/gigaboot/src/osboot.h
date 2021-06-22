// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_OSBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_OSBOOT_H_

#include <stdbool.h>
#include <stdint.h>

#include <efi/protocol/graphics-output.h>
#include <efi/system-table.h>

#define PAGE_SIZE (4096)
#define PAGE_MASK (PAGE_SIZE - 1)

#define BYTES_TO_PAGES(n) (((n) + PAGE_MASK) / PAGE_SIZE)

#define CMDLINE_MAX PAGE_SIZE

// Space for extra ZBI items.
#define EXTRA_ZBI_ITEM_SPACE (8 * PAGE_SIZE)

uint64_t find_acpi_root(efi_handle img, efi_system_table* sys);
uint64_t find_smbios(efi_handle img, efi_system_table* sys);

uint32_t get_zx_pixel_format(efi_graphics_output_protocol* gop);

int boot_deprecated(efi_handle img, efi_system_table* sys, void* image, size_t sz, void* ramdisk,
                    size_t rsz, void* cmdline, size_t csz);

int zbi_boot(efi_handle img, efi_system_table* sys, void* image, size_t sz);

bool image_is_valid(void* image, size_t sz);

// sz may be just one block or sector
// if the header looks like a kernel image, return expected size
// otherwise returns 0
size_t image_getsize(void* imageheader, size_t sz);

// Where to start the kernel from
extern size_t kernel_zone_size;
extern efi_physical_addr kernel_zone_base;

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_OSBOOT_H_
