// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <zircon/boot/multiboot.h>

// The linker script places this first in the image.  Multiboot only
// requires that it appear word-aligned anywhere in the first 8192 bytes.
.section .boot.header, "a", %progbits

// The Multiboot header identifies this as a Multiboot-compliant OS image
// and requires that the boot loader provide the memory map.  The rest of
// of the loading details duplicate the information in the ELF file and
// program headers.  The boot loader should do the same thing whether it
// uses the multiboot_header_t here or the ELF e_entry and PT_LOAD headers.

.object multiboot_header, data, align=4, nosection=nosection
  .int MULTIBOOT_HEADER_MAGIC                             // magic
  .int MULTIBOOT_HEADER_FLAGS                             // flags
  .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS) // checksum
  .int multiboot_header                                   // header_addr
  .int PHYS_LOAD_ADDRESS                                  // load_addr
  .int PHYS_LOAD_END                                      // load_end
  .int _end                                               // bss_end_addr
  .int _start                                             // entry_addr
.end_object
