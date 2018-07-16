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
#define X86_CR4_UMIP                    0x00000800 /* User-mode instruction prevention */
#define X86_CR4_VMXE                    0x00002000 /* enable vmx */
#define X86_CR4_FSGSBASE                0x00010000 /* enable {rd,wr}{fs,gs}base */
#define X86_CR4_PCIDE                   0x00020000 /* Process-context ID enable  */
#define X86_CR4_OSXSAVE                 0x00040000 /* os supports xsave */
#define X86_CR4_SMEP                    0x00100000 /* SMEP protection enabling */
#define X86_CR4_SMAP                    0x00200000 /* SMAP protection enabling */
#define X86_EFER_SCE                    0x00000001 /* enable SYSCALL */
#define X86_EFER_LME                    0x00000100 /* long mode enable */
#define X86_EFER_LMA                    0x00000400 /* long mode active */
#define X86_EFER_NXE                    0x00000800 /* to enable execute disable bit */
#define X86_MSR_IA32_PLATFORM_ID        0x00000017 /* platform id */
#define X86_MSR_IA32_APIC_BASE          0x0000001b /* APIC base physical address */
#define X86_MSR_IA32_TSC_ADJUST         0x0000003b /* TSC adjust */
#define X86_MSR_IA32_BIOS_SIGN_ID       0x0000008b /* BIOS update signature */
#define X86_MSR_IA32_MTRRCAP            0x000000fe /* MTRR capability */
#define X86_MSR_IA32_SYSENTER_CS        0x00000174 /* SYSENTER CS */
#define X86_MSR_IA32_SYSENTER_ESP       0x00000175 /* SYSENTER ESP */
#define X86_MSR_IA32_SYSENTER_EIP       0x00000176 /* SYSENTER EIP */
#define X86_MSR_IA32_MCG_CAP            0x00000179 /* global machine check capability */
#define X86_MSR_IA32_MCG_STATUS         0x0000017a /* global machine check status */
#define X86_MSR_IA32_MISC_ENABLE        0x000001a0 /* enable/disable misc processor features */
#define X86_MSR_IA32_TEMPERATURE_TARGET 0x000001a2 /* Temperature target */
#define X86_MSR_IA32_MTRR_PHYSBASE0     0x00000200 /* MTRR PhysBase0 */
#define X86_MSR_IA32_MTRR_PHYSMASK0     0x00000201 /* MTRR PhysMask0 */
#define X86_MSR_IA32_MTRR_PHYSMASK9     0x00000213 /* MTRR PhysMask9 */
#define X86_MSR_IA32_MTRR_DEF_TYPE      0x000002ff /* MTRR default type */
#define X86_MSR_IA32_MTRR_FIX64K_00000  0x00000250 /* MTRR FIX64K_00000 */
#define X86_MSR_IA32_MTRR_FIX16K_80000  0x00000258 /* MTRR FIX16K_80000 */
#define X86_MSR_IA32_MTRR_FIX16K_A0000  0x00000259 /* MTRR FIX16K_A0000 */
#define X86_MSR_IA32_MTRR_FIX4K_C0000   0x00000268 /* MTRR FIX4K_C0000 */
#define X86_MSR_IA32_MTRR_FIX4K_F8000   0x0000026f /* MTRR FIX4K_F8000 */
#define X86_MSR_IA32_PAT                0x00000277 /* PAT */
#define X86_MSR_IA32_TSC_DEADLINE       0x000006e0 /* TSC deadline */
#define X86_MSR_IA32_EFER               0xc0000080 /* EFER */
#define X86_MSR_IA32_STAR               0xc0000081 /* system call address */
#define X86_MSR_IA32_LSTAR              0xc0000082 /* long mode call address */
#define X86_MSR_IA32_CSTAR              0xc0000083 /* ia32-e compat call address */
#define X86_MSR_IA32_FMASK              0xc0000084 /* system call flag mask */
#define X86_MSR_IA32_FS_BASE            0xc0000100 /* fs base address */
#define X86_MSR_IA32_GS_BASE            0xc0000101 /* gs base address */
#define X86_MSR_IA32_KERNEL_GS_BASE     0xc0000102 /* kernel gs base */
#define X86_MSR_IA32_TSC_AUX            0xc0000103 /* TSC aux */
#define X86_MSR_IA32_PM_ENABLE          0x00000770 /* enable/disable HWP */
#define X86_MSR_IA32_HWP_CAPABILITIES   0x00000771 /* HWP performance range enumeration */
#define X86_MSR_IA32_HWP_REQUEST        0x00000774 /* power manage control hints */
#define X86_CR4_PSE                     0xffffffef /* Disabling PSE bit in the CR4 */

// Non-architectural MSRs
#define X86_MSR_RAPL_POWER_UNIT         0x00000606 /* RAPL unit multipliers */
#define X86_MSR_PKG_POWER_LIMIT         0x00000610 /* Package power limits */
#define X86_MSR_PKG_POWER_LIMIT_PL1_CLAMP   (1 << 16)
#define X86_MSR_PKG_POWER_LIMIT_PL1_ENABLE  (1 << 15)
#define X86_MSR_PKG_POWER_INFO          0x00000614 /* Package power range info */
#define X86_MSR_DRAM_ENERGY_STATUS      0x00000619 /* DRAM energy status */
#define X86_MSR_PP0_ENERGY_STATUS       0x00000639 /* PP0 energy status */
#define X86_MSR_PP1_ENERGY_STATUS       0x00000641 /* PP1 energy status */

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
                                         X86_FLAGS_OF | \
                                         X86_FLAGS_NT | \
                                         X86_FLAGS_AC | \
                                         X86_FLAGS_ID)

#ifndef __ASSEMBLER__

#include <zircon/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Indices of xsave feature states; state components are
 * enumerated in Intel Vol 1 section 13.1 */
#define X86_XSAVE_STATE_INDEX_X87                  0
#define X86_XSAVE_STATE_INDEX_SSE                  1
#define X86_XSAVE_STATE_INDEX_AVX                  2
#define X86_XSAVE_STATE_INDEX_MPX_BNDREG           3
#define X86_XSAVE_STATE_INDEX_MPX_BNDCSR           4
#define X86_XSAVE_STATE_INDEX_AVX512_OPMASK        5
#define X86_XSAVE_STATE_INDEX_AVX512_LOWERZMM_HIGH 6
#define X86_XSAVE_STATE_INDEX_AVX512_HIGHERZMM     7
#define X86_XSAVE_STATE_INDEX_PT                   8
#define X86_XSAVE_STATE_INDEX_PKRU                 9

/* Bit masks for xsave feature states. */
#define X86_XSAVE_STATE_BIT_X87                  (1 << X86_XSAVE_STATE_INDEX_X87)
#define X86_XSAVE_STATE_BIT_SSE                  (1 << X86_XSAVE_STATE_INDEX_SSE)
#define X86_XSAVE_STATE_BIT_AVX                  (1 << X86_XSAVE_STATE_INDEX_AVX)
#define X86_XSAVE_STATE_BIT_MPX_BNDREG           (1 << X86_XSAVE_STATE_INDEX_MPX_BNDREG)
#define X86_XSAVE_STATE_BIT_MPX_BNDCSR           (1 << X86_XSAVE_STATE_INDEX_MPX_BNDCSR)
#define X86_XSAVE_STATE_BIT_AVX512_OPMASK        (1 << X86_XSAVE_STATE_INDEX_AVX512_OPMASK)
#define X86_XSAVE_STATE_BIT_AVX512_LOWERZMM_HIGH (1 << X86_XSAVE_STATE_INDEX_AVX512_LOWERZMM_HIGH)
#define X86_XSAVE_STATE_BIT_AVX512_HIGHERZMM     (1 << X86_XSAVE_STATE_INDEX_AVX512_HIGHERZMM)
#define X86_XSAVE_STATE_BIT_PT                   (1 << X86_XSAVE_STATE_INDEX_PT)
#define X86_XSAVE_STATE_BIT_PKRU                 (1 << X86_XSAVE_STATE_INDEX_PKRU)

// Maximum buffer size needed for xsave and variants. To allocate, see ...BUFFER_SIZE below.
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

/* Initialize a state vector. The passed in buffer must be X86_EXTENDED_REGISTER_SIZE big and it
 * must be 64-byte aligned. This function will initialize it for use in save and restore. */
void x86_extended_register_init_state(void* buffer);

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

struct x86_xsave_legacy_area {
    uint16_t fcw;  /* FPU control word. */
    uint16_t fsw;  /* FPU status word. */
    uint8_t ftw;   /* Abridged FPU tag word (not the same as the FTW register, see
                    * Intel manual sec 10.5.1.1: "x87 State". */
    uint8_t reserved;
    uint16_t fop;  /* FPU opcode. */
    uint64_t fip;  /* FPU instruction pointer. */
    uint64_t fdp;  /* FPU data pointer. */
    uint32_t mxcsr;  /* SSE control status register. */
    uint32_t mxcsr_mask;

    /* The x87/MMX state. For x87 the each "st" entry has the low 80 bits used for the register
     * contents. For MMX, the low 64 bits are used. The higher bits are unused. */
    struct {
        uint64_t low;
        uint64_t high;
    } st[8];

    /* SSE registers. */
    struct {
        uint64_t low;
        uint64_t high;
    } xmm[16];
} __PACKED;

/* Returns the address within the given xsave area of the requested state component. The state
 * component indexes formats are described in section 13.4 of the Intel Software Developer's manual.
 * Use the X86_XSAVE_STATE_INDEX_* macros above for the component indices.
 *
 * The given register state must have previously been filled with the variant of XSAVE that the
 * system is using. Since the save area can be compressed, the offset of each component can vary
 * depending on the contents.
 *
 * The components 0 and 1 are special and refer to the legacy area. In both cases a pointer to the
 * x86_xsave_legacy_area will be returned. Note that "mark_present=true" will only affect the
 * requested component, so if you're writing to both x87 and SSE states, make two separate calls
 * even though the returned pointer will be the same.
 *
 * Some parts of the xsave area are can be marked as unused to optimize. If you plan on
 * writing to the area, set mark_present = true which will ensure that the corresponding area is
 * marked used. Without this, the registers might not be restored when the thread is resumed. This
 * is not currently supported for components >= 2. This means that to set AVX registers, for
 * example, AVX needed to have been previously used by the thread in question. This capability can
 * be added in the future if required.
 *
 * The size of the component will be placed in *size.
 *
 * This function will return null and fill 0 into *size if the component is not present. */
void* x86_get_extended_register_state_component(void* register_state, uint32_t component,
                                                bool mark_present, uint32_t* size);

__END_CDECLS

#endif
