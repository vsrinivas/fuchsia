/* Copyright 2021 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * This file is included as an input linker script to support the assembly code
 * in linuxboot-header.S when used in a phys_executable() target.  It defines
 * the link-time base address needed by phys.ld and also defines values for
 * linuxboot-header.S to use that cannot be represented directly there.
 *
 * The actual load address of the 32-bit (aka "protected mode") code is fixed
 * at 1MiB.  The header defined in linuxboot-header.S tells the boot loader to
 * place it there, for so-called "bzImage" protocol.  The bzImage format starts
 * with the "real-mode kernel header" before that kernel image that contains
 * both header details known as a protocol by boot loaders and also 16-bit x86
 * code that boot loaders optionally use as the entry point.
 *
 * Linux builds a separate 32-bit kernel image (which itself is a pre-kernel
 * that does decompression of an embedded kernel image in another format) and a
 * 16-bit header image and then uses a fixup tool to concatenate them and fix
 * up some fields.  Instead, we do it all in one pass at link time and then
 * just do the usual objcopy extraction to raw binary.  The linuxboot-header.S
 * file provides this header (including 16-bit x86 code) in the .boot.header
 * section that phys.ld will place first in the raw binary image laid out at
 * link time.  That linker script requires PHYS_LOAD_ADDRESS to be defined as
 * the physical address corresponding to the start of that raw binary image.
 */

/*
 * We use only "bzImage" protocol, which loads the 32-bit code at fixed 1MiB.
 */
HIDDEN(LINUXBOOT_LOAD_ADDRESS = 0x100000);

/*
 * So we define PHYS_LOAD_ADDRESS to be beneath the real fixed 1MiB load
 * address of the 32-bit code such that the image directly after .boot.header
 * (linuxboot-header.S) corresponds to that fixed address.  That header content
 * is thus placed just below 1MiB by the linker and starts however far below
 * there that its size requires (aligned in the assembly to a whole number of
 * 512-byte sectors).  The runtime boot protocol may place that header at an
 * arbitrary address, not necessarily right below 1MiB (so the 16-bit code has
 * to be purely position-independent.)  It defines the LINUXBOOT_SETUP_SIZE
 * symbol with the total of its contents in bytes to allow this calculatation.
 */
HIDDEN(PHYS_LOAD_ADDRESS = LINUXBOOT_LOAD_ADDRESS - LINUXBOOT_SETUP_SIZE);

/*
 * The protocol-specified part of the header layout includes the total size of
 * the 32-bit image, which is calculated at link time.  However, it doesn't use
 * a size in bytes for this but a size in 16-byte units (called "paragraphs" in
 * ancient Intelspeak).  There are no x86 relocation types to encode a uint32_t
 * value scaled down by 16 from a link-time symbol address, so assembly code in
 * linuxboot-header.S can't compute this value directly itself.  Instead, it
 * relies on this symbol and stores that value using a simple ABS32 relocation.
 * Linker scripts can do the necessary arithmetic where assembly cannot.
 */
HIDDEN(LINUXBOOT_SYSSIZE = (PHYS_LOAD_END - LINUXBOOT_LOAD_ADDRESS) / 16);

/*
 * This _start symbol just sets e_entry in ELF headers (as per phys.ld), which
 * don't exist at runtime.  Set it to match the 32-bit protocol's entry point.
 * There is no global symbol for the 16-bit entry point (exactly 512 bytes into
 * the .boot.header region) because the linuxboot-header.S code is not actually
 * part of the same memory image as the rest of the program.
 */
HIDDEN(_start = Phys32Entry);
