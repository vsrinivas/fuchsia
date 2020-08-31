// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/riscv64.h>
#include <arch/defines.h>
#include <arch/kernel_aspace.h>
#include <zircon/tls.h>

// This code is purely position-independent and generates no relocations
// that need boot-time fixup; gen-kaslr-fixup.sh ensures this (and would
// ignore it if this code were in .text.boot, so don't put it there).
.text
FUNCTION(_start)
    // set the default stack
    lla     sp, boot_cpu_kstack_end

    // save a0 in zbi_paddr
    lla     t0, zbi_paddr
    sd      a0, (t0)

    // The fixup code appears right after the kernel image (at __data_end in
    // our view).  Note this code overlaps with the kernel's bss!  It
    // expects a0 to contain the current physical address of __code_start and
    // a1 to contain the desired virtual address of __code_start.
    lla     t0, kernel_relocated_base
    ld      s10, (t0)
    mv      a1, s10
    jal     __data_end

    // zero bss
1:
    lla     t0, __bss_start
    lla     t1, _end
0:
    sd      zero, (t0)
    add     t0, t0, 8
    bne     t0, t1, 0b

    // make sure the boot allocator is given a chance to figure out where we
    // are loaded in physical memory
    jal     boot_alloc_init

    // save the physical address the kernel is loaded at
    lla     a0, __code_start
    lla     t0, kernel_base_phys
    sd      a0, (t0)

    // void riscv64_boot_map(pte_t* kernel_table0, vaddr_t vaddr, paddr_t paddr, size_t len, pte_t flags);
    lla     a0, riscv64_kernel_translation_table
    li      a1, 0
    li      a2, 0
    li      a3, ARCH_PHYSMAP_SIZE
    li      a4, (0 | (1<<7) | (1<<6) | (1<<5) | (1<<3) | (1<<2) | (1<<1) | (1<<0))
    jal     riscv64_boot_map

    // map a large run of physical memory at the base of the kernel's address space
    // TODO(fxb/47856): Only map the arenas.
    lla     a0, riscv64_kernel_translation_table
    li      a1, KERNEL_ASPACE_BASE
    li      a2, 0
    li      a3, ARCH_PHYSMAP_SIZE
    li      a4, (0 | (1<<7) | (1<<6) | (1<<5) | (1<<3) | (1<<2) | (1<<1) | (1<<0))
    jal     riscv64_boot_map

    // map the kernel to a fixed address
    // note: mapping the kernel here with full rwx, this will get locked down
    // later in vm initialization;
    lla     a0, riscv64_kernel_translation_table
    lla     t0, kernel_relocated_base
    ld      a1, (t0)
    lla     a2, __code_start
    lla     a3, _end
    sub     a3, a3, a2
    li      a4, (0 | (1<<7) | (1<<6) | (1<<5) | (1<<3) | (1<<2) | (1<<1) | (1<<0))
    jal     riscv64_boot_map

    // ensure it's written out
    fence   w,w

    // set the satp register and enable the mmu
    // ASID 0, riscv64_kernel_translation_table address
    lla     t0, riscv64_kernel_translation_table
    srli    t1, t0, 12
    li      t2, (9 << 60)   // mode 9, SV48
    or      t1, t1, t2
    csrw    satp, t1

    // global tlb fence
    sfence.vma  zero, zero

    // mmu is initialized and we're running out of an identity physical map

    // compute the delta between the old physical and newer high addresses
    lla     t0, __code_start
    sub     t0, s10, t0

    // fix up the gp, stack pointer, and return address
    add     gp, gp, t0
    add     sp, sp, t0
    add     ra, ra, t0

    // jump to high memory
    lla     t1, .Lmmu_on_vaddr
    add     t1, t1, t0
    jr      t1

.Lmmu_on_vaddr:
    // Set up the percpu structure for the logical CPU 0
    lla     x31, percpu

    // call main
    jal     lk_main

    // should never return here
    j       .
END_FUNCTION(_start)

// These are logically .bss (uninitialized data).  But they're set before
// clearing the .bss, so put them in .data so they don't get zeroed.
.data
    .balign 64
DATA(zbi_paddr)
    .quad -1
END_DATA(zbi_paddr)

.bss
LOCAL_DATA(boot_cpu_kstack)
    .skip ARCH_DEFAULT_STACK_SIZE
    .balign 16
LOCAL_DATA(boot_cpu_kstack_end)
END_DATA(boot_cpu_kstack)

// This symbol is used by image.S
.global IMAGE_ELF_ENTRY
IMAGE_ELF_ENTRY = _start

// This symbol is used by gdb python to know the base of the kernel module
.global KERNEL_BASE_ADDRESS
KERNEL_BASE_ADDRESS = KERNEL_BASE
