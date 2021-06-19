// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_

#include <lib/stdcompat/span.h>
#include <zircon/boot/image.h>

#include <cstdint>
#include <string_view>

// This holds information collected from a legacy boot loader protocol.
struct LegacyBoot {
  std::string_view bootloader;
  std::string_view cmdline;
  cpp20::span<std::byte> ramdisk;
  cpp20::span<zbi_mem_range_t> mem_config;
  uint64_t acpi_rsdp = 0;  // Physical address of the ACPI RSDP.
};

// InitMemory() initializes this.
//
// The space pointed to by the members is safe from reclamation by the memory
// allocator after InitMemory().
extern LegacyBoot gLegacyBoot;

// This is a subroutine of InitMemory().  It primes the allocator and reserves
// ranges based on the data in gLegacyBoot.
void InitMemoryFromRanges();

// Set up 64-bit identity-mapping page tables and enable them in the CPU.
// This uses the allocator and so must be done only after all necessary
// memory reservations have been made.
void EnablePaging();

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_
