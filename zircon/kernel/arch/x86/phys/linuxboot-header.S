// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

#include "phys32.h"
#include "linuxboot-asm.h"

// The phys.ld linker script places this first in the image.
// The linuxboot.ld linker script arranges that this means it
// it is "before" the actual fixed load address of the bzImage
// 32-bit code (which includes the start32.S entry point).
.section .boot.header, "awx", %progbits

// This is the putative boot_params object at the start of the image.
// See linuxboot.h for full details about the protocol.  In fact, this
// just corresponds to the boot_params layout for purposes of locating
// the boot_params::setup_header, below.
.org 0
.label zero_page

// This is not used by the Linux/x86 boot protocol.  Historically,
// this format could be used directly as a Master Boot Record image.
// This ancient protocol consists of simply loading the first sector
// into a well-known location in memory and jumping to it in 16-bit
// real mode.  This is not supported by Linux kernels any more, but a
// legacy stub is provided to panic nicely if loaded in the original
// x86 PC way by firmware supporting the old 16-bit x86 BIOS ABI.
.code16
.function MbrEntry, nosection=nosection
  // This is the traditional physical load address.  Make sure
  // it's the exact start of the %cs segment.
  ljmp $0x07c0, $(0f - MbrEntry)
0:

  // Reset segment registers to be safe and don't trust any stack pointer.
  mov %cs, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %ss
  mov %ax, %ss
  xor %sp, %sp

  // Normalize flags that might affect the BIOS or instructions used here.
  sti
  cld

  // cf https://en.wikipedia.org/wiki/BIOS_interrupt_call
  mov $(MbrEntryMessage - MbrEntry), %si
  mov $MbrEntryMessageSize, %cx
.Lbios_write_char:
  lodsb         // %ax = *%si++
  mov $0xe, %ah // Write character in TTY mode.
  mov $0xd, %bx // Color (light magenta).
  int $0x10     // https://en.wikipedia.org/wiki/INT_10H
  loop .Lbios_write_char

.Lbios_wait_for_key:
  xor %ax, %ax
  int $0x16

.Lbios_reboot:
  int $0x19             // Ask the BIOS to reboot.  Should not return.
  ljmp $0xf000, $0xfff0 // If it does, jump directly to the BIOS entry point.
.end_function

.object MbrEntryMessage, nosection=nosection
  .ascii "Legacy x86 MBR booting is not supported.\r\n"
  .ascii "Install a boot loader that supports Linux/x86 bzImage format.\r\n"
  .ascii "Hit any key to reboot...\r\n"
  MbrEntryMessageSize = . - MbrEntryMessage
.end_object

// The space before boot_params::hdr is not examined by the boot loader.  A
// boot loader using the 16-bit entry protocol reads hdr.setup_sects and then
// loads this whole file's contents into memory at zero_page before entering
// at setup_header::jump, below.
.org BOOT_PARAMS_HDR

.object setup_header, nosection=nosection
  // This tells the boot loader how many 512-byte sectors after this first
  // one to load.  LINUXBOOT_SETUP_SIZE is defined at the end of the file.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_SETUP_SECTS
  .byte ((LINUXBOOT_SETUP_SIZE >> 9) - 1)

  // This tells the boot loader how many 16-byte units (Intelspeak paragraphs)
  // to load at the fixed load address for the 32-bit kernel image.  The
  // linuxboot.ld linker script calculates the value of LINUXBOOT_SYSSIZE.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_SYSSIZE
  .int LINUXBOOT_SYSSIZE

  // This is a required magic number the boot loader checks for.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_BOOT_FLAG
  .short LINUXBOOT_BOOT_FLAG

  // This is offset 512, where the 16-bit entry point is.  Since there's a
  // required header field right after, this must be a two-byte instruction.
  // So we make it a jump to past the end of the fixed header layout, where
  // we define the real-mode 16-bit entry path.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_JUMP
  // This must be a 1-byte displacement to make a 2-byte instruction that
  // fits before the next field.  That makes a maximum jump distance of 127,
  // to 0x202 + 127 = 0x281.
  jmp Phys16Entry

  // This is a required magic number the boot loader checks for.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_HEADER
  .ascii "HdrS"

   // This tells the boot loader which precise version of the protocol for
   // encoding bits in these headers this kernel image is compatible with.
   // Version 2.06 is not the newest, but none of the Zircon shim code needs
   // to take advantange of any of the newer features.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_VERSION
  .short 0x0206

   // This indicates "bzImage" format: load the 32-bit code at 1MiB.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_LOADFLAGS
  .byte LOADFLAGS_LOADED_HIGH

   // This gives the boot loader license to place the initrd RAMDISK image
   // anywhere in the 4G address space.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_INITRD_ADDR_MAX
  .int 0xffffffff

   // Load addresses must be aligned to 4KiB.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_KERNEL_ALIGNMENT
  .int 0x1000

   // This constrains the maximum size of kernel command line data passed.
  .org BOOT_PARAMS_HDR + SETUP_HEADER_CMDLINE_SIZE
  .int 0xffffffff

   // The rest of setup_header contains fields a boot loader might examine
   // or modify though they're not meaningful to the version of the protocol
   // we support if they have zero-initialized values.
  .org BOOT_PARAMS_HDR + SIZEOF_SETUP_HEADER
.end_object

// This is close to the farthest the two-byte jump above can reach.
// The boot loader enters at offset 512, which is that short jump here.
.function Phys16Entry, nosection=nosection
  // Make sure interrupts are disabled just in case and normalize the D flag.
  cli
  cld

  // To be safe, only trust %ds to have been set by the boot loader.
  mov %ds, %ax
  mov %ax, %es

  // Copy the setup_header, as modified by the boot loader, into the
  // boot_params struct to be provided to the 32-bit entry path.
  mov $(boot_params + BOOT_PARAMS_HDR - zero_page), %di
  mov $(setup_header - zero_page), %si
  mov $SIZEOF_SETUP_HEADER, %cx
  rep movsb

  // Materialize the linear address of the code below, which will be the
  // new PC in the flat 32-bit code segment.
  mov %ds, %eax
  shl $4, %eax
  add $(Protected32Entry - zero_page), %eax

  // Push the long return address that will be popped below.
  pushl $PHYS32_CODE32_SEL
  .cfi_adjust_cfa_offset 4
  pushl %eax
  .cfi_adjust_cfa_offset 4

  // Now point %esi at the boot_params being filled out here.
  // This is the argument register for the 32-bit entry path.
  mov %ds, %esi
  shl $4, %esi
  add $(boot_params - zero_page), %esi

  // Linux booting protocol requires these all be cleared.  It doesn't really
  // matter to our code, but it makes it consistent with what a boot loader
  // doing direct 32-bit entry will do.
  xor %ebx, %ebx
  xor %ecx, %ecx
  xor %edx, %edx
  xor %ebp, %ebp
  xor %edi, %edi

  // Build a temporary descriptor pointing at the GDT to load it.
  // We reuse the gdt32.cc setup that start32.S will load again later.
  // But we need it loaded first so we can enable 32-bit protected mode.
  movl $gPhys32Gdt, -4(%esp)
  movw $(PHYS32_GDT_SIZE - 1), -6(%esp)
  lgdtl -6(%esp)

  // Enable protected mode.  The Intel manual recommends doing a far jump
  // into a new %cs segment immediately after setting the PE bit.
  mov %cr0, %eax
  orb $X86_CR0_PE, %al
  mov %eax, %cr0

  // Immediately return into the new 32-bit code segment.
  lretl
.end_function

// Execution resumes here, now in 32-bit protected mode.
// This code still has to be purely position-independent.
.code32
.function Protected32Entry, align=16, nosection=nosection
  // Reload all the segment registers (except %cs).  The Intel manual
  // recommends doing this immediately after jumping into the new %cs segment.
  // But clearing a few registers first is always safe enough.
  mov $PHYS32_DATA32_SEL, %ax // Flat data segment for %ds, %es, and %ss.
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %ss
  mov %bx, %fs // Null segment selector for %fs and %gs.
  mov %bx, %gs

  // Now jump to the start32.S entry point at its fixed address.
  mov $Phys32Entry, %eax
  jmp *%eax
.end_function

// When the 16-bit entry path is used, it passes this to the 32-bit path after
// copying setup_header, as previously modified by the boot loader, into it.
// The 16-bit setup code here is the only source of information other than what
// the boot loader wrote into setup_header.  When the boot loader uses the
// 32-bit entry path, it passes its own pointer to a boot_params struct it has
// allocated elsewhere and filled with appropriate data, including copying our
// setup_header into that struct.  In that case this space is not used at all.
.balign 16, 0
.object boot_params, nosection=nosection
  .space SIZEOF_BOOT_PARAMS, 0
.end_object

// Pad out to a whole number of 512-byte sectors and record that total size (in
// bytes) as the value of the LINUXBOOT_SETUP_SIZE symbol (a SHN_ABS symbol
// that's a size, not an address).  The setup_header::setup_sects field
// initializer is calculated from this value above.  The linuxboot.ld linker
// script also uses this symbol to compute the LINUXBOOT_SYSSIZE symbol that's
// needed for the setup_header::syssize field initializer above.
.balign 512, 0
.label LINUXBOOT_SETUP_SIZE, global, notype, . - zero_page
