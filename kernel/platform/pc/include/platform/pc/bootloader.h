// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/boot/bootdata.h>

// Data passed in by the bootloader
// Used by various bits of pc platform init

typedef struct pc_bootloader_info {
    uint64_t acpi_rsdp;

    void* efi_system_table;

    void* efi_mmap;
    size_t efi_mmap_size;

    void* e820_table;
    size_t e820_count;

    uint64_t ramdisk_base;
    size_t ramdisk_size;

    bootdata_swfb_t fb;
    bootdata_uart_t uart;
    bootdata_nvram_t nvram;
} pc_bootloader_info_t;

extern pc_bootloader_info_t bootloader;