// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trampoline.h"

#include <inttypes.h>
#include <stddef.h>
#include <zircon/compiler.h>

// Populate the trampoline area and enter the kernel in 64-bit mode.
// Paging is already enabled.  The page tables, the kernel and ZBI images,
// and the trampoline area are all placed safely outside the kernel's
// range: PHYS_LOAD_ADDRESS + kernel image size + kernel bss size.
noreturn void boot_zbi(zircon_kernel_t* kernel, zbi_header_t* zbi,
                              struct trampoline* trampoline) {
    // The trampoline area holds the 64-bit trampoline code we'll run, the
    // GDT with the 64-bit code segment we'll run it in, and the long jump
    // descriptor we'll use to get there.
    *trampoline = (struct trampoline){
        .code = TRAMPOLINE_CODE,
        .gdt = GDT_ENTRIES,
        .ljmp = {
            .eip = trampoline->code,
            .cs = 1 << 3,
        },
    };

    // The kernel image includes its own container header.
    size_t kernel_size = sizeof(kernel->hdr_file) + kernel->hdr_file.length;

    // The entry point is an absolute physical address.
    uint32_t kernel_entry = kernel->data_kernel.entry;
    if (unlikely(kernel_entry != kernel->data_kernel.entry)) {
        panic("ZBI kernel entry point %#llx truncated to %#"PRIx32,
              kernel->data_kernel.entry, kernel_entry);
    }
    if (unlikely(kernel_entry < (uintptr_t)PHYS_LOAD_ADDRESS ||
                 kernel_entry >= (uintptr_t)PHYS_LOAD_ADDRESS + kernel_size)) {
        panic("ZBI kernel entry point %#"PRIx32" outside kernel [%p, %p)",
              kernel_entry, PHYS_LOAD_ADDRESS,
              PHYS_LOAD_ADDRESS + kernel_size);
    }

    // The descriptor needed to load the new GDT can be placed on the stack.
    const struct { uint16_t limit; void* base; } __PACKED lgdt = {
        .base = trampoline->gdt,
        .limit = sizeof(trampoline->gdt) - 1,
    };

    // Tell the compiler all of the trampoline area is read.
    // Otherwise it might conclude that only gdt and ljmp are used.
    __asm__ volatile("" :: "m"(*trampoline));

    __asm__ volatile(
        // Load the GDT stored safely in the trampoline area.  We can
        // access the descriptor via the stack segment and stack pointer
        // using the Multiboot-provided flat segments.  Hereafter we can
        // use only the registers and the already-running code and data
        // segments, since there are no 32-bit segments in the new GDT.
        "lgdt %[lgdt]\n\t"
        // Jump into the 64-bit trampoline code.  The jump descriptor
        // resides in the trampoline area, so the compiler will access it
        // through a non-stack register here.
        "ljmp *%[ljmp]\n\t"
        :: [lgdt]"m"(lgdt), [ljmp]"m"(trampoline->ljmp),
         // The 64-bit trampoline code copies the kernel into place and
         // then jumps to its entry point, as instructed here:
         "D"(PHYS_LOAD_ADDRESS),       // %rdi: destination pointer
         "S"(kernel),                  // %rsi: source pointer
         "c"(kernel_size / 8),         // %rcx: count of 8-byte words
         "a"(kernel_entry),            // %rax: kernel entry point
         "b"(zbi)                      // %rbx: ZBI data pointer for kernel
        );
    __builtin_unreachable();
}
