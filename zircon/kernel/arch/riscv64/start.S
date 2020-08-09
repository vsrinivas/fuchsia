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

    // make sure the boot allocator is given a chance to figure out where we
    // are loaded in physical memory
    jal     boot_alloc_init

    // save the physical address the kernel is loaded at
    lla     a0, __code_start
    lla     t0, kernel_base_phys
    sd      a0, (t0)

    // zero bss
1:
    lla     t0, __bss_start
    lla     t1, _end
0:
    sd      zero, (t0)
    add     t0, t0, 8
    bne     t0, t1, 0b

    // initialize the kernel page tables
    // for all MMU versions, identity map some amount of memory near 0 and
    // the same amount at the bottom of the kernel's address space
    lla     t0, kernel_pgtable

    // store the physical address of the pgtable for future use
    lla     t1, kernel_pgtable_phys
    sd      t0, (t1)

    // compute kernel pgtable pointer (index 256)
    addi    t1, t0, (8 * 128)
    addi    t1, t1, (8 * 128)

    // page table entry: address 0, A, D, G, XWR, V
    li      t2, (0 | (1<<7) | (1<<6) | (1<<5) | (1<<3) | (1<<2) | (1<<1) | (1<<0))

    // num interations and increment count
    // RV48: map the first 512GB of the physical address space at the
    // bottom of the kernel address space using a single terapage
    li      t3, 1
    li      t4, (512 * 1024 * 1024 * 1024) >> 2

    // loop, writing t3 entries out and incrementing by t4 address
0:
    sd      t2, (t1)
    sd      t2, (t0)
    add     t2, t2, t4
    addi    t0, t0, 8
    addi    t1, t1, 8
    addi    t3, t3, -1
    bnez    t3, 0b

    // ensure it's written out
    fence   w,w

    // set the satp register and enable the mmu
    // ASID 0, kernel_pgtable address
    lla     t0, kernel_pgtable
    srli    t1, t0, 12
    li      t2, (9 << 60)   // mode 9, SV48
    or      t1, t1, t2
    csrw    satp, t1

    // global tlb fence
    sfence.vma  zero, zero

    // mmu is initialized and we're running out of an identity physical map

    // save the physical address of .Lhigh
    lla     t1, .Lhigh

    // bounce to the high address
    lla     t0, .Lhigh_addr
    ld      t0, (t0)
    jr      t0

    // the full virtual address of the .Lhigh label
.Lhigh_addr:
    .quad   .Lhigh
.Lhigh:

    // we're now running at the high virtual address
    // compute the delta between the old physical and newer high addresses
    sub     t0, t0, t1

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
