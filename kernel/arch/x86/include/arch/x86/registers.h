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
#define X86_CR4_VMXE                    0x00002000 /* enable vmx */
#define X86_CR4_FSGSBASE                0x00010000 /* enable {rd,wr}{fs,gs}base */
#define X86_CR4_OSXSAVE                 0x00040000 /* os supports xsave */
#define X86_CR4_SMEP                    0x00100000 /* SMEP protection enabling */
#define X86_CR4_SMAP                    0x00200000 /* SMAP protection enabling */
#define X86_EFER_SCE                    0x00000001 /* enable SYSCALL */
#define X86_EFER_LME                    0x00000100 /* long mode enable */
#define X86_EFER_LMA                    0x00000400 /* long mode active */
#define X86_EFER_NXE                    0x00000800 /* to enable execute disable bit */
#define X86_MSR_IA32_APIC_BASE          0x0000001b /* APIC base physical address model-specific register */
#define X86_MSR_IA32_TSC_ADJUST         0x0000003b /* TSC adjust model-specific register */
#define X86_MSR_IA32_MTRRCAP            0x000000fe /* MTRR capability model-specific register */
#define X86_MSR_IA32_MTRR_PHYSBASE0     0x00000200 /* MTRR PhysBase0 model-specific register */
#define X86_MSR_IA32_MTRR_PHYSMASK0     0x00000201 /* MTRR PhysMask0 model-specific register */
#define X86_MSR_IA32_MTRR_PHYSMASK9     0x00000213 /* MTRR PhysMask9 model-specific register */
#define X86_MSR_IA32_MTRR_DEF_TYPE      0x000002ff /* MTRR default type model-specific register */
#define X86_MSR_IA32_MTRR_FIX64K_00000  0x00000250 /* MTRR FIX64K_00000 model-specific register */
#define X86_MSR_IA32_MTRR_FIX16K_80000  0x00000258 /* MTRR FIX16K_80000 model-specific register */
#define X86_MSR_IA32_MTRR_FIX16K_A0000  0x00000259 /* MTRR FIX16K_A0000 model-specific register */
#define X86_MSR_IA32_MTRR_FIX4K_C0000   0x00000268 /* MTRR FIX4K_C0000 model-specific register */
#define X86_MSR_IA32_MTRR_FIX4K_F8000   0x0000026f /* MTRR FIX4K_F8000 model-specific register */
#define X86_MSR_IA32_PAT                0x00000277 /* PAT model-specific register */
#define X86_MSR_IA32_TSC_DEADLINE       0x000006e0 /* TSC deadline model-specific register */
#define X86_MSR_IA32_EFER               0xc0000080 /* EFER model-specific register */
#define X86_MSR_IA32_STAR               0xc0000081 /* system call address */
#define X86_MSR_IA32_LSTAR              0xc0000082 /* long mode call address */
#define X86_MSR_IA32_FMASK              0xc0000084 /* system call flag mask */
#define X86_MSR_IA32_FS_BASE            0xc0000100 /* fs base address */
#define X86_MSR_IA32_GS_BASE            0xc0000101 /* gs base address */
#define X86_MSR_IA32_KERNEL_GS_BASE     0xc0000102 /* kernel gs base */
#define X86_CR4_PSE                     0xffffffef /* Disabling PSE bit in the CR4 */

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
#define X86_FLAGS_RESERVED_ONES         0x2
#define X86_FLAGS_RESERVED              0xffc0802a
#define X86_FLAGS_USER                  (X86_FLAGS_CF | \
                                         X86_FLAGS_PF | \
                                         X86_FLAGS_AF | \
                                         X86_FLAGS_ZF | \
                                         X86_FLAGS_SF | \
                                         X86_FLAGS_TF | \
                                         X86_FLAGS_DF | \
                                         X86_FLAGS_OF)

#ifndef ASSEMBLY

#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Bit masks for xsave feature states; state components are
 * enumerated in Intel Vol 1 section 13.1 */
#define X86_XSAVE_STATE_X87                  (1<<0)
#define X86_XSAVE_STATE_SSE                  (1<<1)
#define X86_XSAVE_STATE_AVX                  (1<<2)
#define X86_XSAVE_STATE_MPX_BNDREG           (1<<3)
#define X86_XSAVE_STATE_MPX_BNDCSR           (1<<4)
#define X86_XSAVE_STATE_AVX512_OPMASK        (1<<5)
#define X86_XSAVE_STATE_AVX512_LOWERZMM_HIGH (1<<6)
#define X86_XSAVE_STATE_AVX512_HIGHERZMM     (1<<7)
#define X86_XSAVE_STATE_PT                   (1<<8)
#define X86_XSAVE_STATE_PKRU                 (1<<9)

#define X86_MAX_EXTENDED_REGISTER_SIZE 1024

enum x86_extended_register_feature {
    X86_EXTENDED_REGISTER_X87,
    X86_EXTENDED_REGISTER_SSE,
    X86_EXTENDED_REGISTER_AVX,
    X86_EXTENDED_REGISTER_MPX,
    X86_EXTENDED_REGISTER_AVX512,
    X86_EXTENDED_REGISTER_PT,
    X86_EXTENDED_REGISTER_PKRU,
};

/* Identify which extended registers are supported.  Also initialize
 * the FPU if present */
void x86_extended_register_init(void);

/* Enable the requested feature on this CPU, return true on success.
 * It is currently assumed that if a feature is enabled on one CPU, the caller
 * will ensure it is enabled on all CPUs */
bool x86_extended_register_enable_feature(enum x86_extended_register_feature);

size_t x86_extended_register_size(void);
/* Initialize a state vector */
void x86_extended_register_init_state(void *register_state);
/* Save current state to state vector */
void x86_extended_register_save_state(void *register_state);
/* Restore a state created by x86_extended_register_init_state or
 * x86_extended_register_save_state */
void x86_extended_register_restore_state(void *register_state);

typedef struct thread thread_t;
void x86_extended_register_context_switch(
        thread_t *old_thread, thread_t *new_thread);

void x86_set_extended_register_pt_state(bool threads);

uint64_t x86_xgetbv(uint32_t reg);
void x86_xsetbv(uint32_t reg, uint64_t val);

__END_CDECLS

#endif
