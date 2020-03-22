// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_BOOTLOADER_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_BOOTLOADER_H_

#include <stdint.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <ktl/variant.h>

// Data passed in by the bootloader
// Used by various bits of pc platform init

struct pc_bootloader_info_t {
  uint64_t acpi_rsdp;
  uint64_t smbios;

  void* efi_system_table;

  void* efi_mmap;
  size_t efi_mmap_size;

  void* e820_table;
  size_t e820_count;

  uint64_t ramdisk_base;
  size_t ramdisk_size;

  zbi_swfb_t fb;
  ktl::variant<ktl::monostate, dcfg_simple_pio_t, dcfg_simple_t> uart;
  zbi_nvram_t nvram;

  uint64_t platform_id_size;
  zbi_platform_id_t platform_id;
};

extern pc_bootloader_info_t bootloader;

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_BOOTLOADER_H_
