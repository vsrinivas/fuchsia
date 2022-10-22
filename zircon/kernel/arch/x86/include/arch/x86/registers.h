// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_REGISTERS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_REGISTERS_H_

// clang-format off

// This header is intended to be included in both C and ASM
#define X86_CR0_PE 0x00000001               /* protected mode enable */
#define X86_CR0_MP 0x00000002               /* monitor coprocessor */
#define X86_CR0_EM 0x00000004               /* emulation */
#define X86_CR0_TS 0x00000008               /* task switched */
#define X86_CR0_ET 0x00000010               /* extension type */
#define X86_CR0_NE 0x00000020               /* enable x87 exception */
#define X86_CR0_WP 0x00010000               /* supervisor write protect */
#define X86_CR0_NW 0x20000000               /* not write-through */
#define X86_CR0_CD 0x40000000               /* cache disable */
#define X86_CR0_PG 0x80000000               /* enable paging */
#define X86_CR4_PAE 0x00000020              /* PAE paging */
#define X86_CR4_PGE 0x00000080              /* page global enable */
#define X86_CR4_OSFXSR 0x00000200           /* os supports fxsave */
#define X86_CR4_OSXMMEXPT 0x00000400        /* os supports xmm exception */
#define X86_CR4_UMIP 0x00000800             /* User-mode instruction prevention */
#define X86_CR4_VMXE 0x00002000             /* enable vmx */
#define X86_CR4_FSGSBASE 0x00010000         /* enable {rd,wr}{fs,gs}base */
#define X86_CR4_PCIDE 0x00020000            /* Process-context ID enable  */
#define X86_CR4_OSXSAVE 0x00040000          /* os supports xsave */
#define X86_CR4_SMEP 0x00100000             /* SMEP protection enabling */
#define X86_CR4_SMAP 0x00200000             /* SMAP protection enabling */
#define X86_CR4_PKE 0x00400000              /* Enable protection keys */
#define X86_EFER_SCE 0x00000001             /* enable SYSCALL */
#define X86_EFER_LME 0x00000100             /* long mode enable */
#define X86_EFER_LMA 0x00000400             /* long mode active */
#define X86_EFER_NXE 0x00000800             /* to enable execute disable bit */
#define X86_MSR_IA32_PLATFORM_ID 0x00000017 /* platform id */
#define X86_MSR_IA32_APIC_BASE 0x0000001b   /* APIC base physical address */
#define X86_MSR_IA32_TSC_ADJUST 0x0000003b  /* TSC adjust */
#define X86_MSR_IA32_SPEC_CTRL 0x00000048   /* Speculative Execution Controls */
#define X86_SPEC_CTRL_IBRS (1ull << 0)
// Partitions indirect branch predictors across hyperthreads
#define X86_SPEC_CTRL_STIBP (1ull << 1) /* Single Thread Indirect Branch Predictors */
#define X86_SPEC_CTRL_SSBD (1ull << 2)
#define X86_MSR_SMI_COUNT 0x00000034            /* Number of SMI interrupts since boot */
#define X86_MSR_IA32_PRED_CMD 0x00000049        /* Indirect Branch Prediction Command */
#define X86_MSR_IA32_BIOS_UPDT_TRIG 0x00000079u /* Microcode Patch Loader */
#define X86_MSR_IA32_BIOS_SIGN_ID 0x0000008b    /* BIOS update signature */
#define X86_MSR_IA32_MTRRCAP 0x000000fe         /* MTRR capability */
#define X86_MSR_IA32_ARCH_CAPABILITIES 0x0000010a
#define X86_ARCH_CAPABILITIES_RDCL_NO (1ull << 0)
#define X86_ARCH_CAPABILITIES_IBRS_ALL (1ull << 1)
#define X86_ARCH_CAPABILITIES_RSBA (1ull << 2)
#define X86_ARCH_CAPABILITIES_SSB_NO (1ull << 4)
#define X86_ARCH_CAPABILITIES_MDS_NO (1ull << 5)
#define X86_ARCH_CAPABILITIES_TSX_CTRL (1ull << 7)
#define X86_ARCH_CAPABILITIES_TAA_NO (1ull << 8)
#define X86_MSR_IA32_FLUSH_CMD 0x0000010b          /* L1D$ Flush control */
#define X86_MSR_IA32_TSX_CTRL 0x00000122           /* Control to enable/disable TSX instructions */
#define X86_TSX_CTRL_RTM_DISABLE (1ull << 0)       /* Force all RTM instructions to abort */
#define X86_TSX_CTRL_CPUID_DISABLE (1ull << 1)     /* Mask RTM and HLE in CPUID */
#define X86_MSR_IA32_SYSENTER_CS 0x00000174        /* SYSENTER CS */
#define X86_MSR_IA32_SYSENTER_ESP 0x00000175       /* SYSENTER ESP */
#define X86_MSR_IA32_SYSENTER_EIP 0x00000176       /* SYSENTER EIP */
#define X86_MSR_IA32_MCG_CAP 0x00000179            /* global machine check capability */
#define X86_MSR_IA32_MCG_STATUS 0x0000017a         /* global machine check status */
#define X86_MSR_IA32_MISC_ENABLE 0x000001a0        /* enable/disable misc processor features */
#define X86_MSR_IA32_MISC_ENABLE_TURBO_DISABLE (1ull << 38)
#define X86_MSR_IA32_TEMPERATURE_TARGET 0x000001a2 /* Temperature target */
#define X86_MSR_IA32_ENERGY_PERF_BIAS 0x000001b0   /* Energy / Performance Bias */
#define X86_MSR_IA32_MTRR_PHYSBASE0 0x00000200     /* MTRR PhysBase0 */
#define X86_MSR_IA32_MTRR_PHYSMASK0 0x00000201     /* MTRR PhysMask0 */
#define X86_MSR_IA32_MTRR_PHYSMASK9 0x00000213     /* MTRR PhysMask9 */
#define X86_MSR_IA32_MTRR_DEF_TYPE 0x000002ff      /* MTRR default type */
#define X86_MSR_IA32_MTRR_FIX64K_00000 0x00000250  /* MTRR FIX64K_00000 */
#define X86_MSR_IA32_MTRR_FIX16K_80000 0x00000258  /* MTRR FIX16K_80000 */
#define X86_MSR_IA32_MTRR_FIX16K_A0000 0x00000259  /* MTRR FIX16K_A0000 */
#define X86_MSR_IA32_MTRR_FIX4K_C0000 0x00000268   /* MTRR FIX4K_C0000 */
#define X86_MSR_IA32_MTRR_FIX4K_F8000 0x0000026f   /* MTRR FIX4K_F8000 */
#define X86_MSR_IA32_PAT 0x00000277                /* PAT */
#define X86_MSR_IA32_TSC_DEADLINE 0x000006e0       /* TSC deadline */

#define X86_MSR_IA32_X2APIC_APICID      0x00000802 /* x2APIC ID Register (R/O) */
#define X86_MSR_IA32_X2APIC_VERSION     0x00000803 /* x2APIC Version Register (R/O) */
#define X86_MSR_IA32_X2APIC_TPR         0x00000808 /* x2APIC Task Priority Register (R/W) */
#define X86_MSR_IA32_X2APIC_PPR         0x0000080A /* x2APIC Processor Priority Register (R/O) */
#define X86_MSR_IA32_X2APIC_EOI         0x0000080B /* x2APIC EOI Register (W/O) */
#define X86_MSR_IA32_X2APIC_LDR         0x0000080D /* x2APIC Logical Destination Register (R/O) */
#define X86_MSR_IA32_X2APIC_SIVR        0x0000080F /* x2APIC Spurious Interrupt Vector Register (R/W) */
#define X86_MSR_IA32_X2APIC_ISR0        0x00000810 /* x2APIC In-Service Register Bits 31:0 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR1        0x00000811 /* x2APIC In-Service Register Bits 63:32 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR2        0x00000812 /* x2APIC In-Service Register Bits 95:64 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR3        0x00000813 /* x2APIC In-Service Register Bits 127:96 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR4        0x00000814 /* x2APIC In-Service Register Bits 159:128 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR5        0x00000815 /* x2APIC In-Service Register Bits 191:160 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR6        0x00000816 /* x2APIC In-Service Register Bits 223:192 (R/O) */
#define X86_MSR_IA32_X2APIC_ISR7        0x00000817 /* x2APIC In-Service Register Bits 255:224 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR0        0x00000818 /* x2APIC Trigger Mode Register Bits 31:0 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR1        0x00000819 /* x2APIC Trigger Mode Register Bits 63:32 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR2        0x0000081A /* x2APIC Trigger Mode Register Bits 95:64 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR3        0x0000081B /* x2APIC Trigger Mode Register Bits 127:96 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR4        0x0000081C /* x2APIC Trigger Mode Register Bits 159:128 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR5        0x0000081D /* x2APIC Trigger Mode Register Bits 191:160 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR6        0x0000081E /* x2APIC Trigger Mode Register Bits 223:192 (R/O) */
#define X86_MSR_IA32_X2APIC_TMR7        0x0000081F /* x2APIC Trigger Mode Register Bits 255:224 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR0        0x00000820 /* x2APIC Interrupt Request Register Bits 31:0 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR1        0x00000821 /* x2APIC Interrupt Request Register Bits 63:32 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR2        0x00000822 /* x2APIC Interrupt Request Register Bits 95:64 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR3        0x00000823 /* x2APIC Interrupt Request Register Bits 127:96 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR4        0x00000824 /* x2APIC Interrupt Request Register Bits 159:128 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR5        0x00000825 /* x2APIC Interrupt Request Register Bits 191:160 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR6        0x00000826 /* x2APIC Interrupt Request Register Bits 223:192 (R/O) */
#define X86_MSR_IA32_X2APIC_IRR7        0x00000827 /* x2APIC Interrupt Request Register Bits 255:224 (R/O) */
#define X86_MSR_IA32_X2APIC_ESR         0x00000828 /* x2APIC Error Status Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_CMCI    0x0000082F /* x2APIC LVT Corrected Machine Check Interrupt Register (R/W) */
#define X86_MSR_IA32_X2APIC_ICR         0x00000830 /* x2APIC Interrupt Command Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_TIMER   0x00000832 /* x2APIC LVT Timer Interrupt Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_THERMAL 0x00000833 /* x2APIC LVT Thermal Sensor Interrupt Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_PMI     0x00000834 /* x2APIC LVT Performance Monitor Interrupt Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_LINT0   0x00000835 /* x2APIC LVT LINT0 Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_LINT1   0x00000836 /* x2APIC LVT LINT1 Register (R/W) */
#define X86_MSR_IA32_X2APIC_LVT_ERROR   0x00000837 /* x2APIC LVT Error Register (R/W) */
#define X86_MSR_IA32_X2APIC_INIT_COUNT  0x00000838 /* x2APIC Initial Count Register (R/W) */
#define X86_MSR_IA32_X2APIC_CUR_COUNT   0x00000839 /* x2APIC Current Count Register (R/O) */
#define X86_MSR_IA32_X2APIC_DIV_CONF    0x0000083E /* x2APIC Divide Configuration Register (R/W) */
#define X86_MSR_IA32_X2APIC_SELF_IPI    0x0000083F /* x2APIC Self IPI Register (W/O) */

#define X86_MSR_IA32_EFER 0xc0000080               /* EFER */
#define X86_MSR_IA32_STAR 0xc0000081               /* system call address */
#define X86_MSR_IA32_LSTAR 0xc0000082              /* long mode call address */
#define X86_MSR_IA32_CSTAR 0xc0000083              /* ia32-e compat call address */
#define X86_MSR_IA32_FMASK 0xc0000084              /* system call flag mask */
#define X86_MSR_IA32_FS_BASE 0xc0000100            /* fs base address */
#define X86_MSR_IA32_GS_BASE 0xc0000101            /* gs base address */
#define X86_MSR_IA32_KERNEL_GS_BASE 0xc0000102     /* kernel gs base */
#define X86_MSR_IA32_TSC_AUX 0xc0000103            /* TSC aux */
#define X86_MSR_IA32_PM_ENABLE 0x00000770          /* enable/disable HWP */
#define X86_MSR_IA32_HWP_CAPABILITIES 0x00000771   /* HWP performance range enumeration */
#define X86_MSR_IA32_HWP_REQUEST 0x00000774        /* power manage control hints */
#define X86_MSR_AMD_VIRT_SPEC_CTRL 0xc001011f      /* AMD speculative execution controls */
                                                   /* See IA32_SPEC_CTRL */
#define X86_CR4_PSE 0xffffffef                     /* Disabling PSE bit in the CR4 */

// Non-architectural MSRs
#define X86_MSR_POWER_CTL 0x000001fc               /* Power Control Register */
#define X86_MSR_RAPL_POWER_UNIT 0x00000606         /* RAPL unit multipliers */
#define X86_MSR_PKG_POWER_LIMIT 0x00000610         /* Package power limits */
#define X86_MSR_PKG_ENERGY_STATUS 0x00000611       /* Package energy status */
#define X86_MSR_PKG_POWER_INFO 0x00000614          /* Package power range info */
#define X86_MSR_DRAM_POWER_LIMIT 0x00000618        /* DRAM RAPL power limit control */
#define X86_MSR_DRAM_ENERGY_STATUS 0x00000619      /* DRAM energy status */
#define X86_MSR_PP0_POWER_LIMIT 0x00000638         /* PP0 RAPL power limit control */
#define X86_MSR_PP0_ENERGY_STATUS 0x00000639       /* PP0 energy status */
#define X86_MSR_PP1_POWER_LIMIT 0x00000640         /* PP1 RAPL power limit control */
#define X86_MSR_PP1_ENERGY_STATUS 0x00000641       /* PP1 energy status */
#define X86_MSR_PLATFORM_ENERGY_COUNTER 0x0000064d /* Platform energy counter */
#define X86_MSR_PPERF 0x0000064e                   /* Productive performance count */
#define X86_MSR_PERF_LIMIT_REASONS 0x0000064f      /* Clipping cause register */
#define X86_MSR_GFX_PERF_LIMIT_REASONS 0x000006b0  /* Clipping cause register for graphics */
#define X86_MSR_PLATFORM_POWER_LIMIT 0x0000065c    /* Platform power limit control */
#define X86_MSR_AMD_F10_DE_CFG 0xc0011029          /* AMD Family 10h+ decode config */
#define X86_MSR_AMD_F10_DE_CFG_LFENCE_SERIALIZE (1 << 1)

#define X86_MSR_AMD_LS_CFG 0xc0011020 /* Load/store unit configuration */
#define X86_AMD_LS_CFG_F15H_SSBD (1ull << 54)
#define X86_AMD_LS_CFG_F16H_SSBD (1ull << 33)
#define X86_AMD_LS_CFG_F17H_SSBD (1ull << 10)
#define X86_MSR_K7_HWCR 0xc0010015                 /* AMD Hardware Configuration */
#define X86_MSR_K7_HWCR_CPB_DISABLE (1ull << 25)   /* Set to disable turbo ('boost') */

// KVM MSRs
#define X86_MSR_KVM_PV_EOI_EN 0x4b564d04           /* Enable paravirtual fast APIC EOI */
#define X86_MSR_KVM_PV_EOI_EN_ENABLE (1ull << 0)

/* EFLAGS/RFLAGS */
#define X86_FLAGS_CF (1 << 0)
#define X86_FLAGS_PF (1 << 2)
#define X86_FLAGS_AF (1 << 4)
#define X86_FLAGS_ZF (1 << 6)
#define X86_FLAGS_SF (1 << 7)
#define X86_FLAGS_TF (1 << 8)
#define X86_FLAGS_IF (1 << 9)
#define X86_FLAGS_DF (1 << 10)
#define X86_FLAGS_OF (1 << 11)
#define X86_FLAGS_STATUS_MASK (0xfff)
#define X86_FLAGS_IOPL_MASK (3 << 12)
#define X86_FLAGS_IOPL_SHIFT (12)
#define X86_FLAGS_NT (1 << 14)
#define X86_FLAGS_RF (1 << 16)
#define X86_FLAGS_VM (1 << 17)
#define X86_FLAGS_AC (1 << 18)
#define X86_FLAGS_VIF (1 << 19)
#define X86_FLAGS_VIP (1 << 20)
#define X86_FLAGS_ID (1 << 21)
#define X86_FLAGS_RESERVED_ONES 0x2
#define X86_FLAGS_RESERVED 0xffc0802a
#define X86_FLAGS_USER                                                                       \
  (X86_FLAGS_CF | X86_FLAGS_PF | X86_FLAGS_AF | X86_FLAGS_ZF | X86_FLAGS_SF | X86_FLAGS_TF | \
   X86_FLAGS_DF | X86_FLAGS_OF | X86_FLAGS_NT | X86_FLAGS_AC | X86_FLAGS_ID)

/* DR6 */
#define X86_DR6_B0 (1ul << 0)
#define X86_DR6_B1 (1ul << 1)
#define X86_DR6_B2 (1ul << 2)
#define X86_DR6_B3 (1ul << 3)
#define X86_DR6_BD (1ul << 13)
#define X86_DR6_BS (1ul << 14)
#define X86_DR6_BT (1ul << 15)

// NOTE: DR6 is used as a read-only status registers, and it is not writeable through userspace.
//       Any bits attempted to be written will be ignored.
#define X86_DR6_USER_MASK \
  (X86_DR6_B0 | X86_DR6_B1 | X86_DR6_B2 | X86_DR6_B3 | X86_DR6_BD | X86_DR6_BS | X86_DR6_BT)
/* Only bits in X86_DR6_USER_MASK are writeable.
 * Bits 12 and 32:63 must be written with 0, the rest as 1s */
#define X86_DR6_MASK (0xffff0ff0ul)

/* DR7 */
#define X86_DR7_L0 (1ul << 0)
#define X86_DR7_G0 (1ul << 1)
#define X86_DR7_L1 (1ul << 2)
#define X86_DR7_G1 (1ul << 3)
#define X86_DR7_L2 (1ul << 4)
#define X86_DR7_G2 (1ul << 5)
#define X86_DR7_L3 (1ul << 6)
#define X86_DR7_G3 (1ul << 7)
#define X86_DR7_LE (1ul << 8)
#define X86_DR7_GE (1ul << 9)
#define X86_DR7_GD (1ul << 13)
#define X86_DR7_RW0 (3ul << 16)
#define X86_DR7_LEN0 (3ul << 18)
#define X86_DR7_RW1 (3ul << 20)
#define X86_DR7_LEN1 (3ul << 22)
#define X86_DR7_RW2 (3ul << 24)
#define X86_DR7_LEN2 (3ul << 26)
#define X86_DR7_RW3 (3ul << 28)
#define X86_DR7_LEN3 (3ul << 30)

// NOTE1: Even though the GD bit is writable, we disable it for the write_state syscall because it
//        complicates a lot the reasoning about how to access the registers. This is because
//        enabling this bit would make any other access to debug registers to issue an exception.
//        New syscalls should be define to lock/unlock debug registers.
// NOTE2: LE/GE bits are normally ignored, but the manual recommends always setting it to 1 in
//        order to be backwards compatible. Hence they are not writable from userspace.
#define X86_DR7_USER_MASK                                                                     \
  (X86_DR7_L0 | X86_DR7_G0 | X86_DR7_L1 | X86_DR7_G1 | X86_DR7_L2 | X86_DR7_G2 | X86_DR7_L3 | \
   X86_DR7_G3 | X86_DR7_RW0 | X86_DR7_LEN0 | X86_DR7_RW1 | X86_DR7_LEN1 | X86_DR7_RW2 |       \
   X86_DR7_LEN2 | X86_DR7_RW3 | X86_DR7_LEN3)

/* Bits 11:12, 14:15 and 32:63 must be cleared to 0. Bit 10 must be set to 1. */
#define X86_DR7_MASK ((1ul << 10) | X86_DR7_LE | X86_DR7_GE)

#define HW_DEBUG_REGISTERS_COUNT 4

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>

/* Indices of xsave feature states; state components are
 * enumerated in Intel Vol 1 section 13.1 */
#define X86_XSAVE_STATE_INDEX_X87 0
#define X86_XSAVE_STATE_INDEX_SSE 1
#define X86_XSAVE_STATE_INDEX_AVX 2
#define X86_XSAVE_STATE_INDEX_MPX_BNDREG 3
#define X86_XSAVE_STATE_INDEX_MPX_BNDCSR 4
#define X86_XSAVE_STATE_INDEX_AVX512_OPMASK 5
#define X86_XSAVE_STATE_INDEX_AVX512_LOWERZMM_HIGH 6
#define X86_XSAVE_STATE_INDEX_AVX512_HIGHERZMM 7
#define X86_XSAVE_STATE_INDEX_PT 8
#define X86_XSAVE_STATE_INDEX_PKRU 9

/* Bit masks for xsave feature states. */
#define X86_XSAVE_STATE_BIT_X87 (1 << X86_XSAVE_STATE_INDEX_X87)
#define X86_XSAVE_STATE_BIT_SSE (1 << X86_XSAVE_STATE_INDEX_SSE)
#define X86_XSAVE_STATE_BIT_AVX (1 << X86_XSAVE_STATE_INDEX_AVX)
#define X86_XSAVE_STATE_BIT_MPX_BNDREG (1 << X86_XSAVE_STATE_INDEX_MPX_BNDREG)
#define X86_XSAVE_STATE_BIT_MPX_BNDCSR (1 << X86_XSAVE_STATE_INDEX_MPX_BNDCSR)
#define X86_XSAVE_STATE_BIT_AVX512_OPMASK (1 << X86_XSAVE_STATE_INDEX_AVX512_OPMASK)
#define X86_XSAVE_STATE_BIT_AVX512_LOWERZMM_HIGH (1 << X86_XSAVE_STATE_INDEX_AVX512_LOWERZMM_HIGH)
#define X86_XSAVE_STATE_BIT_AVX512_HIGHERZMM (1 << X86_XSAVE_STATE_INDEX_AVX512_HIGHERZMM)
#define X86_XSAVE_STATE_BIT_PT (1 << X86_XSAVE_STATE_INDEX_PT)
#define X86_XSAVE_STATE_BIT_PKRU (1 << X86_XSAVE_STATE_INDEX_PKRU)

// Maximum buffer size needed for xsave and variants. To allocate, see ...BUFFER_SIZE below.
#define X86_MAX_EXTENDED_REGISTER_SIZE 1024

// clang-format on

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
void x86_extended_register_init();

/* Enable the requested feature on this CPU, return true on success.
 * It is currently assumed that if a feature is enabled on one CPU, the caller
 * will ensure it is enabled on all CPUs */
bool x86_extended_register_enable_feature(enum x86_extended_register_feature);

/* Return the size required for all requested features. */
size_t x86_extended_register_size();

/* Return the size required for all supported features, whether requested or not. */
size_t x86_extended_register_max_size();

/* Return all potentially supported (although possibly not currently enabled) state bits for xcr0 */
uint64_t x86_extended_xcr0_component_bitmap();

/* Returns whether or not xsave is supported by the CPU */
bool x86_xsave_supported();

/* Initialize a state vector. The passed in buffer must be X86_EXTENDED_REGISTER_SIZE big and it
 * must be 64-byte aligned. This function will initialize it for use in save and restore. */
void x86_extended_register_init_state(void* buffer);

/* Initialize a state vector to a specific set of state bits. The passed in buffer must be
 * X86_EXTENDED_REGISTER_SIZE big and it must be 64-byte aligned. This function will initialize it
 * for use in save and restore. */
void x86_extended_register_init_state_from_bv(void* register_state, uint64_t xstate_bv);

/* Save current state to state vector */
void x86_extended_register_save_state(void* register_state);

/* Restore a state created by x86_extended_register_init_state or
 * x86_extended_register_save_state */
void x86_extended_register_restore_state(const void* register_state);

struct Thread;
void x86_extended_register_context_switch(Thread* old_thread, const Thread* new_thread);

void x86_set_extended_register_pt_state(bool threads);

uint64_t x86_xgetbv(uint32_t reg);
void x86_xsetbv(uint32_t reg, uint64_t val);

struct x86_xsave_legacy_area {
  uint16_t fcw; /* FPU control word. */
  uint16_t fsw; /* FPU status word. */
  uint8_t ftw;  /* Abridged FPU tag word (not the same as the FTW register, see
                 * Intel manual sec 10.5.1.1: "x87 State". */
  uint8_t reserved;
  uint16_t fop;   /* FPU opcode. */
  uint64_t fip;   /* FPU instruction pointer. */
  uint64_t fdp;   /* FPU data pointer. */
  uint32_t mxcsr; /* SSE control status register. */
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

/* Kernel tracking of the current state of the x86 debug registers for a particular thread */
typedef struct x86_debug_state {
  uint64_t dr[4];
  uint64_t dr6;
  uint64_t dr7;
} x86_debug_state_t;

/* Disables the HW debug functionalities for the current thread.
 * There is no "enable" call. To do this, use the x86_write_debug_state call. */
void x86_disable_debug_state();

/* Checks whether the given state is valid to install on a running thread.
 * Will mask out reserved values on DR6 and DR7. This is for the caller convenience, considering
 * that we don't have a good mechanism to communicate back to the user what went wrong with the
 * call. */
bool x86_validate_debug_state(x86_debug_state_t* debug_state);

/* Only update the status section of |debug_state| (DR6). All other state will not be modified */
void x86_read_debug_status(uint64_t* dr6);

void x86_write_debug_status(uint64_t dr6);

/* Read from the CPU registers into |debug_state|. */
void x86_read_hw_debug_regs(x86_debug_state_t* debug_state);

/* Write from the |debug_state| into the CPU registers.
 *
 * IMPORTANT: This function is used in the context switch, so no validation is done, just writing.
 *            In any other context (eg. setting debug values from a syscall), you *MUST* call
 *            x86_validate_debug_state first. */
void x86_write_hw_debug_regs(const x86_debug_state_t* debug_state);

#ifndef NDEBUG

void x86_print_dr6(uint64_t dr6);
void x86_print_dr7(uint64_t dr7);

#endif  // !NDEBUG

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_REGISTERS_H_
