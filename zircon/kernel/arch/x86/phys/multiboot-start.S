// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <zircon/boot/multiboot.h>

// This is the Multiboot entry point.  The boot loader passes arguments
// in %eax and %ebx.  A Multiboot-compliant boot loader provides a
// usable 32-bit segmentation environment with flat segments, but does
// not provide a stack.
.function _start, global
  mov %ebx, %esi
  cmp $MULTIBOOT_BOOTLOADER_MAGIC, %eax
  je Phys32Entry

.Lbad_multiboot_magic:
  ud2
  jmp .Lbad_multiboot_magic
.end_function
