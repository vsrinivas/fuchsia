// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

// The phys.ld linker script places this first in the image.
.section .boot.header, "awx", %progbits

// cf https://www.kernel.org/doc/html/latest/arm64/booting.html

.org 0

// The boot CPU enters here with x0 pointing at the Deviceetree FDT header.
.function LinuxbootEntry, nosection=nosection
  // This is a harmless instruction that also happens to have "MZ" as its
  // first two (little-endian) bytes, which is the EFI (PE-COFF) magic number.
  // Some boot loaders look for this, apparently.
  ccmp x18, #0x0, #0xd, pl

  // Jump to the start.S code, which passes x0 up to PhysMain.
  b _start
.end_function

// Linux boot loaders may look for this header.  (A loader that simply slaps
// the kernel image someplace in memory and jumps there will usually work too.)
.object LinuxbootHeader, nosection=nosection
  // The initial two instructions are sort of part of the header.
  .org 8

  // Image load offset.
  .quad 0

  // Size of the kernel in memory.  This includes the bss after the image
  // so that the boot loader is sure to place the kernel where it has
  // enough headroom after the storage image is loaded.  This symbol is
  // defined by the linuxboot.ld linker script.
  .quad LINUXBOOT_SIZE

  // Flags: place anywhere (aligned to 2MiB, but we only presume 64KiB anyway)
  .quad 0x4

  // An obvious magic number that boot loaders will check for.
  .org 56
  .ascii "ARM\x64"

  // For EFI booting, this is an offset to PE-COFF header stuff.  We don't
  // define all that.  This shim is meant for Linux-specific boot loaders.
  // Booting Zircon on EFI will be done by a proper EFI-based loader/shim.
  .org 60
  .int 0
.end_object
