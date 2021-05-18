// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_

#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

// This holds information collected from a legacy boot loader protocol.
struct LegacyBoot {
  ktl::string_view bootloader;
  ktl::string_view cmdline;
  ktl::span<ktl::byte> ramdisk;
  ktl::span<zbi_mem_range_t> mem_config;
};

// InitMemory() initializes this.
//
// The space pointed to by the members is safe from reclamation by the memory
// allocator after InitMemory().
extern LegacyBoot gLegacyBoot;

// This is a subroutine of InitMemory().  It primes the allocator and reserves
// ranges based on the data in gLegacyBoot.
void InitMemoryFromRanges();

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_LEGACY_BOOT_H_
