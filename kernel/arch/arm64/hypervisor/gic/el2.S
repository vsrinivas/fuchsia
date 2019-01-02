// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/hypervisor/el2_state.h>
#include <arch/asm_macros.h>
#include <asm.h>
#include <zircon/errors.h>

// For details please refer to ARM Generic Interrupt Controller Architecture
// Specification, version 3.0 and version 4.0. Table 8-6 Mapping of MSR and MRS
// to virtual interface control registers, AArch64 state and Table 8-8 Mapping
// of MCR and MRC to virtual interface control registers, AArch32 state.

#define ICH_AP0R(x)         S3_4_C12_C8_ ## x
#define ICH_AP1R(x)         S3_4_C12_C9_ ## x
#define ICH_HCR_EL2         S3_4_C12_C11_0
#define ICH_VTR_EL2         S3_4_C12_C11_1
#define ICH_MISR_EL2        S3_4_C12_C11_2
#define ICH_ELRSR_EL2       S3_4_C12_C11_5
#define ICH_VMCR_EL2        S3_4_C12_C11_7
#define ICH_LR0(x)          S3_4_C12_C12_ ## x
#define ICH_LR8(x)          S3_4_C12_C13_ ## x

#define ICH_LR0_EL2         ICH_LR0(0)
#define ICH_LR1_EL2         ICH_LR0(1)
#define ICH_LR2_EL2         ICH_LR0(2)
#define ICH_LR3_EL2         ICH_LR0(3)
#define ICH_LR4_EL2         ICH_LR0(4)
#define ICH_LR5_EL2         ICH_LR0(5)
#define ICH_LR6_EL2         ICH_LR0(6)
#define ICH_LR7_EL2         ICH_LR0(7)
#define ICH_LR8_EL2         ICH_LR8(0)
#define ICH_LR9_EL2         ICH_LR8(1)
#define ICH_LR10_EL2        ICH_LR8(2)
#define ICH_LR11_EL2        ICH_LR8(3)
#define ICH_LR12_EL2        ICH_LR8(4)
#define ICH_LR13_EL2        ICH_LR8(5)
#define ICH_LR14_EL2        ICH_LR8(6)
#define ICH_LR15_EL2        ICH_LR8(7)

#define ICH_LR(x)           ICH_LR ## x ## _EL2

#define READ_SYSREG         0
#define WRITE_SYSREG        1

.section .text.el2,"ax",@progbits
.align 12

// Temporary registers to use.
reg_xa  .req x11
reg_xb  .req x12
reg_wb  .req w12
reg_xc  .req x13

.macro read_sysreg sysreg, off
    mrs reg_xa, \sysreg
    str reg_xa, [x0, \off]
.endm

.macro write_sysreg sysreg, off
    ldr reg_xa, [x0, \off]
    msr \sysreg, reg_xa
.endm

// Calculate the jump table index from a value in memory:
//   reg_xb = max - x0[off]
.macro gic_index max, off
    ldurb reg_wb, [x0, \off]
    mov reg_xc, \max
    sub reg_xb, reg_xc, reg_xb
.endm

// Branch to an address within a jump table, calculated as follows:
//   address = table + (reg_xb << 3)
.macro gic_jump table
    adr reg_xc, \table
    add reg_xc, reg_xc, reg_xb, lsl 3
    br reg_xc
.endm

// void arm64_el2_gicv3_read_gich_state(zx_paddr_t state)
FUNCTION(arm64_el2_gicv3_read_gich_state)
    mov reg_xa, READ_SYSREG
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_state)

// void arm64_el2_gicv3_write_gich_state(zx_paddr_t state, uint32_t hcr)
FUNCTION(arm64_el2_gicv3_write_gich_state)
    mov reg_xa, WRITE_SYSREG
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_state)

FUNCTION_LABEL(el2_hvc_gich_state)
    cbnz reg_xa, el2_hvc_write_gich_state

FUNCTION_LABEL(el2_hvc_read_gich_state)
    msr ICH_HCR_EL2, xzr
    read_sysreg ICH_VMCR_EL2, IS_VMCR
    read_sysreg ICH_MISR_EL2, IS_MISR
    read_sysreg ICH_ELRSR_EL2, IS_ELRSR

    gic_index 4, IS_NUM_APRS
    gic_jump .Lread_ap0r
.Lread_ap0r:
    read_sysreg ICH_AP0R(3), IS_APR(0, 3)
    read_sysreg ICH_AP0R(2), IS_APR(0, 2)
    read_sysreg ICH_AP0R(1), IS_APR(0, 1)
    read_sysreg ICH_AP0R(0), IS_APR(0, 0)

    gic_jump .Lread_ap1r
.Lread_ap1r:
    read_sysreg ICH_AP1R(3), IS_APR(1, 3)
    read_sysreg ICH_AP1R(2), IS_APR(1, 2)
    read_sysreg ICH_AP1R(1), IS_APR(1, 1)
    read_sysreg ICH_AP1R(0), IS_APR(1, 0)

    gic_index 16, IS_NUM_LRS
    gic_jump .Llr_read_lr
.Llr_read_lr:
    read_sysreg ICH_LR(15), IS_LR(15)
    read_sysreg ICH_LR(14), IS_LR(14)
    read_sysreg ICH_LR(13), IS_LR(13)
    read_sysreg ICH_LR(12), IS_LR(12)
    read_sysreg ICH_LR(11), IS_LR(11)
    read_sysreg ICH_LR(10), IS_LR(10)
    read_sysreg ICH_LR(9), IS_LR(9)
    read_sysreg ICH_LR(8), IS_LR(8)
    read_sysreg ICH_LR(7), IS_LR(7)
    read_sysreg ICH_LR(6), IS_LR(6)
    read_sysreg ICH_LR(5), IS_LR(5)
    read_sysreg ICH_LR(4), IS_LR(4)
    read_sysreg ICH_LR(3), IS_LR(3)
    read_sysreg ICH_LR(2), IS_LR(2)
    read_sysreg ICH_LR(1), IS_LR(1)
    read_sysreg ICH_LR(0), IS_LR(0)

    b el2_gicv3_done

FUNCTION_LABEL(el2_hvc_write_gich_state)
    msr ICH_HCR_EL2, x1
    write_sysreg ICH_VMCR_EL2, IS_VMCR

    gic_index 4, IS_NUM_APRS
    gic_jump .Lwrite_ap0r
.Lwrite_ap0r:
    write_sysreg ICH_AP0R(3), IS_APR(0, 3)
    write_sysreg ICH_AP0R(2), IS_APR(0, 2)
    write_sysreg ICH_AP0R(1), IS_APR(0, 1)
    write_sysreg ICH_AP0R(0), IS_APR(0, 0)

    gic_jump .Lwrite_ap1r
.Lwrite_ap1r:
    write_sysreg ICH_AP1R(3), IS_APR(1, 3)
    write_sysreg ICH_AP1R(2), IS_APR(1, 2)
    write_sysreg ICH_AP1R(1), IS_APR(1, 1)
    write_sysreg ICH_AP1R(0), IS_APR(1, 0)

    gic_index 16, IS_NUM_LRS
    gic_jump .Llr_write_lr
.Llr_write_lr:
    write_sysreg ICH_LR(15), IS_LR(15)
    write_sysreg ICH_LR(14), IS_LR(14)
    write_sysreg ICH_LR(13), IS_LR(13)
    write_sysreg ICH_LR(12), IS_LR(12)
    write_sysreg ICH_LR(11), IS_LR(11)
    write_sysreg ICH_LR(10), IS_LR(10)
    write_sysreg ICH_LR(9), IS_LR(9)
    write_sysreg ICH_LR(8), IS_LR(8)
    write_sysreg ICH_LR(7), IS_LR(7)
    write_sysreg ICH_LR(6), IS_LR(6)
    write_sysreg ICH_LR(5), IS_LR(5)
    write_sysreg ICH_LR(4), IS_LR(4)
    write_sysreg ICH_LR(3), IS_LR(3)
    write_sysreg ICH_LR(2), IS_LR(2)
    write_sysreg ICH_LR(1), IS_LR(1)
    write_sysreg ICH_LR(0), IS_LR(0)

    b el2_gicv3_done

// uint32_t arm64_el2_gicv3_read_gich_vtr()
FUNCTION(arm64_el2_gicv3_read_gich_vtr)
    hvc 6
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_vtr)

FUNCTION_LABEL(el2_hvc_gich_vtr)
    mrs x0, ICH_VTR_EL2
    b el2_gicv3_done

FUNCTION_LABEL(el2_gicv3_done)
    msr vttbr_el2, xzr
    isb
    eret
