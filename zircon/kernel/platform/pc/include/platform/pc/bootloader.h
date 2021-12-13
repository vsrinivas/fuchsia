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

#include <ktl/span.h>
#include <ktl/variant.h>

// Data passed in by the bootloader
// Used by various bits of pc platform init

struct pc_bootloader_info_t {
  uint64_t acpi_rsdp;
  zbi_swfb_t fb;
};

extern pc_bootloader_info_t bootloader;

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_BOOTLOADER_H_
