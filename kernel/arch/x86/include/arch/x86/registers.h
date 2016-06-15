// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// This header is intended to be included in both C and ASM
#define X86_CR0_PE                      0x00000001 /* protected mode enable */
#define X86_CR0_MP                      0x00000002 /* monitor coprocessor */
#define X86_CR0_EM                      0x00000004 /* emulation */
#define X86_CR0_TS                      0x00000008 /* task switched */
#define X86_CR0_NE                      0x00000020 /* enable x87 exception */
#define X86_CR0_WP                      0x00010000 /* supervisor write protect */
#define X86_CR0_NW                      0x20000000 /* not write-through */
#define X86_CR0_CD                      0x40000000 /* cache disable */
#define X86_CR0_PG                      0x80000000 /* enable paging */
#define X86_CR4_PAE                     0x00000020 /* PAE paging */
#define X86_CR4_PGE                     0x00000080 /* page global enable */
#define X86_CR4_OSFXSR                  0x00000200 /* os supports fxsave */
#define X86_CR4_OSXMMEXPT               0x00000400 /* os supports xmm exception */
#define X86_CR4_OSXSAVE                 0x00040000 /* os supports xsave */
#define X86_CR4_SMEP                    0x00100000 /* SMEP protection enabling */
#define X86_CR4_SMAP                    0x00200000 /* SMAP protection enabling */
#define X86_EFER_SCE                    0x00000001 /* enable SYSCALL */
#define X86_EFER_LME                    0x00000100 /* long mode enable */
#define X86_EFER_LMA                    0x00000400 /* long mode active */
#define X86_EFER_NXE                    0x00000800 /* to enable execute disable bit */
#define X86_MSR_EFER                    0xc0000080 /* EFER Model Specific Register id */
#define X86_MSR_IA32_STAR               0xc0000081 /* system call address */
#define X86_MSR_IA32_LSTAR              0xc0000082 /* long mode call address */
#define X86_MSR_IA32_FMASK              0xc0000084 /* system call flag mask */
#define X86_MSR_IA32_FS_BASE            0xc0000100 /* fs base address */
#define X86_MSR_IA32_GS_BASE            0xc0000101 /* gs base address */
#define X86_MSR_IA32_KERNEL_GS_BASE     0xc0000102 /* kernel gs base */
#define X86_CR4_PSE 0xffffffef /* Disabling PSE bit in the CR4 */

/* EFLAGS/RFLAGS */
#define X86_FLAGS_CF                    (1<<0)
#define X86_FLAGS_PF                    (1<<2)
#define X86_FLAGS_AF                    (1<<4)
#define X86_FLAGS_ZF                    (1<<6)
#define X86_FLAGS_SF                    (1<<7)
#define X86_FLAGS_TF                    (1<<8)
#define X86_FLAGS_IF                    (1<<9)
#define X86_FLAGS_DF                    (1<<10)
#define X86_FLAGS_OF                    (1<<11)
#define X86_FLAGS_STATUS_MASK           (0xfff)
#define X86_FLAGS_IOPL_MASK             (3<<12)
#define X86_FLAGS_IOPL_SHIFT            (12)
#define X86_FLAGS_NT                    (1<<14)
#define X86_FLAGS_RF                    (1<<16)
#define X86_FLAGS_VM                    (1<<17)
#define X86_FLAGS_AC                    (1<<18)
#define X86_FLAGS_VIF                   (1<<19)
#define X86_FLAGS_VIP                   (1<<20)
#define X86_FLAGS_ID                    (1<<21)
