// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>
#include <arch/x86/asm.h>
#include <zircon/boot/multiboot.h>

// Multiboot runs in 32-bit protected mode.
.code32

// The Multiboot header identifies this as a Multiboot-compliant OS image
// and requires that the boot loader provide the memory map.  The rest of
// of the loading details duplicate the information in the ELF file and
// program headers.  The boot loader should do the same thing whether it
// uses the multiboot_header_t here or the ELF e_entry and PT_LOAD headers.
.section .multiboot.header,"a",%progbits
.balign 4
DATA(multiboot_header)
    .int MULTIBOOT_HEADER_MAGIC                             // magic
    .int MULTIBOOT_HEADER_FLAGS                             // flags
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS) // checksum
    .int multiboot_header                                   // header_addr
    .int PHYS_LOAD_ADDRESS                                  // load_addr
    .int edata                                              // load_end
    .int end                                                // bss_end_addr
    .int _start                                             // entry_addr
END_DATA(multiboot_header)

// This is the Multiboot entry point.  The boot loader passes arguments in
// %eax and %ebx.  The C regparm ABI takes them in %eax and %edx.  A
// Multiboot-compliant boot loader provides a usable 32-bit segmentation
// environment with flat segments, but does not provide a stack.
.section .text._start,"a",%progbits
FUNCTION(_start)
    // Save the first argument from the boot loader.
    mov %eax, %edx

    // First off, zero the bss area.
    cld
    xor %eax, %eax
    mov $edata, %edi
    mov $end, %ecx
    sub %edi, %ecx
    rep stosb

    // Switch to our stack.
    mov $stack_end, %esp
    xor %ebp, %ebp

    // Clear the IDT to zero address and zero limit, so any trap is sure to
    // get a triple-fault.  We've just cleared the bss containing the stack,
    // so these bytes are known to be zero.
    lidt -6(%esp)

    // Move the arguments into place and call:
    // noreturn void multiboot_main(uint32_t magic, multiboot_info_t* info);
    mov %edx, %eax
    mov %ebx, %edx
    call multiboot_main

    // Fault if the C code returns.
0:  ud2
    jmp 0b
END_FUNCTION(_start)

.section .bss.stack,"aw",%nobits
DATA(stack)
    .skip 4096
    .balign 16
stack_end:
END_DATA(stack)
