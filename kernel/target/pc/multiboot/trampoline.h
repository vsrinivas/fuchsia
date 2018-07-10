// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include <zircon/boot/image.h>
#include <zircon/boot/multiboot.h>
#include <zircon/compiler.h>

// x86-64 code for: `memcpy(rdi, rsi, rcx * 8); (*rax)(rsi=rbx);`
#define TRAMPOLINE_CODE {                                               \
    0xf3, 0x48, 0xa5,                        /* rep movsq */            \
    0x48, 0x89, 0xde,                        /* mov %rbx, %rsi */       \
    0xff, 0xe0,                              /* jmp *%rax */            \
}

// Initializers for GDT entries as uint64_t.
#define GDT_ENTRIES {                                                   \
    0, /* Null entry for selector 0. */                                 \
    /* 64-bit code segment with base zero, the only one we need. */     \
    0xffffull |               /* limit 15:00 */                         \
    (0b10011010ull << 40) |   /* P(1) DPL(00) S(1) 1 C(0) R(1) A(0) */  \
    (0b10101111ull << 48),    /* G(1) D(0) L(1) AVL(0) limit 19:16 */   \
}

// This is stored in some "safe" memory that won't be overwritten by
// the kernel immediately.  It contains everything needed to copy the
// kernel into place and run it in 64-bit mode.  Copying it into place
// will overwrite the code in this file, so nothing here can run after
// jumping to the trampoline code.
struct trampoline {
    uint8_t code[sizeof((const uint8_t[])TRAMPOLINE_CODE)];
    alignas(8) uint64_t gdt[
        sizeof((const uint64_t[])GDT_ENTRIES) / sizeof(uint64_t)];
    struct {
        void* eip;
        uint16_t cs;
        uint16_t pad;
    } ljmp;
};

// This is defined by the linker script.
extern uint8_t PHYS_LOAD_ADDRESS[];

noreturn void boot_zbi(zircon_kernel_t* kernel, zbi_header_t* zbi,
                              struct trampoline* trampoline);

// This is the entry point called from multiboot-start.S.  It's in the
// environment required by the Multiboot spec, but given a small stack
// and the C (regparm) calling convention.
noreturn void multiboot_main(uint32_t magic, multiboot_info_t* info);

void enable_64bit_paging(uintptr_t start, uintptr_t end);

__PRINTFLIKE(1, 2) static inline noreturn void panic(const char* msg, ...) {
    while (true) {
        __builtin_trap();
    }
}
