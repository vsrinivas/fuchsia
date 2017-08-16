// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <efi/system-table.h>
#include <efi/protocol/graphics-output.h>

#define PAGE_SIZE (4096)
#define PAGE_MASK (PAGE_SIZE - 1)

#define BYTES_TO_PAGES(n) (((n) + PAGE_MASK) / PAGE_SIZE)

// Ensure there are some pages preceeding the
// Ramdisk so that the kernel start code can
// use them to prepend bootdata items if desired.
#define FRONT_PAGES (8)
#define FRONT_BYTES (PAGE_SIZE * FRONT_PAGES)

#define CMDLINE_MAX PAGE_SIZE

int boot_kernel(efi_handle img, efi_system_table* sys,
                void* image, size_t sz,
                void* ramdisk, size_t rsz);

uint64_t find_acpi_root(efi_handle img, efi_system_table* sys);

uint32_t get_mx_pixel_format(efi_graphics_output_protocol* gop);

int boot_deprecated(efi_handle img, efi_system_table* sys,
                    void* image, size_t sz,
                    void* ramdisk, size_t rsz,
                    void* cmdline, size_t csz);

int mxboot(efi_handle img, efi_system_table* sys,
           void* image, size_t sz);