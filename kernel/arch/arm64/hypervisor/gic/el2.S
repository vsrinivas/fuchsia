// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/asm_macros.h>
#include <asm.h>
#include <zircon/errors.h>


// For details please refer to ARM Generic Interrupt Controller Architecture Specification,
// GIC architecture version 3.0 and version 4.0. Table 8-6 Mapping of MSR and MRS to
// virtual interface control registers, AArch64 state and Table 8-8 Mapping of MCR and
// MRC to virtual interface control registers, AArch32 state.
#define ICH_HCR_EL2                     S3_4_C12_C11_0
#define ICH_VTR_EL2                     S3_4_C12_C11_1
#define ICH_MISR_EL2                    S3_4_C12_C11_2
#define ICH_ELRSR_EL2                   S3_4_C12_C11_5
#define ICH_AP0R0_EL2                   S3_4_C12_C8_0
#define ICH_VMCR_EL2                    S3_4_C12_C11_7

#define ICH_HCR_EL2_ID                  0
#define ICH_VTR_EL2_ID                  1
#define ICH_MISR_EL2_ID                 2
#define ICH_ELRSR_EL2_ID                3
#define ICH_AP0R0_EL2_ID                4
#define ICH_VMCR_EL2_ID                 5
#define ICH_LR_EL2_ID                   6

#define LR0_EL2(x)                      S3_4_C12_C12_ ## x
#define LR8_EL2(x)                      S3_4_C12_C13_ ## x

#define ICH_LR0                         LR0_EL2(0)
#define ICH_LR1                         LR0_EL2(1)
#define ICH_LR2                         LR0_EL2(2)
#define ICH_LR3                         LR0_EL2(3)
#define ICH_LR4                         LR0_EL2(4)
#define ICH_LR5                         LR0_EL2(5)
#define ICH_LR6                         LR0_EL2(6)
#define ICH_LR7                         LR0_EL2(7)
#define ICH_LR8                         LR8_EL2(0)
#define ICH_LR9                         LR8_EL2(1)
#define ICH_LR10                        LR8_EL2(2)
#define ICH_LR11                        LR8_EL2(3)
#define ICH_LR12                        LR8_EL2(4)
#define ICH_LR13                        LR8_EL2(5)
#define ICH_LR14                        LR8_EL2(6)
#define ICH_LR15                        LR8_EL2(7)

#define ICH_LR(x)                       ICH_LR ## x

#define READ_SYSREG_ID            0
#define WRITE_SYSREG_ID           1

.section .text.el2,"ax",@progbits
.align 12

.macro read_sysreg literal
    mrs x0, \literal
    b el2_gicv3_done
.endm

.macro write_sysreg literal
    msr \literal, x0
    b el2_gicv3_done
.endm

.macro invalid_write
    b el2_gicv3_done
    nop
.endm

// Branch to an address within a jump table, calculated as follows:
//   address = table + (reg << 3)
.macro gic_jump table reg
    adr x9, \table
    add x9, x9, \reg, lsl 3
    br x9
.endm

// x0 - Value to write
// x1 - Index for ICH register
// x2 - READ/WRITE
// x3 - Register name/enum
FUNCTION_LABEL(el2_hvc_sysreg)
    cbnz x2, .write_sys

    gic_jump .Lread_sys x3

.Lread_sys:
    read_sysreg ICH_HCR_EL2
    read_sysreg ICH_VTR_EL2
    read_sysreg ICH_MISR_EL2
    read_sysreg ICH_ELRSR_EL2
    read_sysreg ICH_AP0R0_EL2
    read_sysreg ICH_VMCR_EL2
    b el2_gicv3_read_lr

.write_sys:
    gic_jump .Lwrite_sys x3

.Lwrite_sys:
    write_sysreg ICH_HCR_EL2
    invalid_write // ICH_VTR_EL2 is readonly
    invalid_write // ICH_MISR_EL2 is readonly
    invalid_write // ICH_ELRSR_EL2 is readonly
    write_sysreg ICH_AP0R0_EL2
    write_sysreg ICH_VMCR_EL2
    b el2_gicv3_write_lr

// uint32_t arm64_el2_gicv3_read_gich_hcr();
FUNCTION(arm64_el2_gicv3_read_gich_hcr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_HCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_hcr)

// void arm64_el2_gicv3_write_gich_hcr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_hcr)
    mov x2, WRITE_SYSREG_ID
    mov x3, ICH_HCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_hcr)

// uint32_t arm64_el2_gicv3_read_gich_vtr();
FUNCTION(arm64_el2_gicv3_read_gich_vtr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_VTR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_vtr)

// uint32_t arm64_el2_gicv3_read_gich_vmcr();
FUNCTION(arm64_el2_gicv3_read_gich_vmcr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_VMCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_vmcr)

// void arm64_el2_gicv3_write_gich_vmcr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_vmcr)
    mov x2, WRITE_SYSREG_ID
    mov x3, ICH_VMCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_vmcr)

// uint32_t arm64_el2_gicv3_read_gich_elrsr();
FUNCTION(arm64_el2_gicv3_read_gich_elrsr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_ELRSR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_elrsr)

// uint32_t arm64_el2_gicv3_read_gich_apr();
FUNCTION(arm64_el2_gicv3_read_gich_apr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_AP0R0_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_apr)

// void arm64_el2_gicv3_write_gich_apr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_apr)
    mov x2, WRITE_SYSREG_ID
    mov x3, ICH_AP0R0_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_apr)

// uint32_t arm64_el2_gicv3_read_gich_misr();
FUNCTION(arm64_el2_gicv3_read_gich_misr)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_MISR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_misr)

// uint64_t arm64_el2_gicv3_read_gich_lr(uint32_t index);
FUNCTION(arm64_el2_gicv3_read_gich_lr)
    mov x1, x0
    mov x2, READ_SYSREG_ID
    mov x3, ICH_LR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_lr)

// void arm64_el2_gicv3_write_gich_lr(uint64_t val, uint32_t index)
FUNCTION(arm64_el2_gicv3_write_gich_lr)
    mov x2, WRITE_SYSREG_ID
    mov x3, ICH_LR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_lr)

// x1 - Index for LR register
FUNCTION_LABEL(el2_gicv3_read_lr)
    gic_jump .Llr_read_table x1

.Llr_read_table:
    read_sysreg ICH_LR(0)
    read_sysreg ICH_LR(1)
    read_sysreg ICH_LR(2)
    read_sysreg ICH_LR(3)
    read_sysreg ICH_LR(4)
    read_sysreg ICH_LR(5)
    read_sysreg ICH_LR(6)
    read_sysreg ICH_LR(7)
    read_sysreg ICH_LR(8)
    read_sysreg ICH_LR(9)
    read_sysreg ICH_LR(10)
    read_sysreg ICH_LR(11)
    read_sysreg ICH_LR(12)
    read_sysreg ICH_LR(13)
    read_sysreg ICH_LR(14)
    read_sysreg ICH_LR(15)

// x0 - Value to write
// x1 - Index for LR register
FUNCTION_LABEL(el2_gicv3_write_lr)
    gic_jump .Llr_write_table x1

.Llr_write_table:
    write_sysreg ICH_LR(0)
    write_sysreg ICH_LR(1)
    write_sysreg ICH_LR(2)
    write_sysreg ICH_LR(3)
    write_sysreg ICH_LR(4)
    write_sysreg ICH_LR(5)
    write_sysreg ICH_LR(6)
    write_sysreg ICH_LR(7)
    write_sysreg ICH_LR(8)
    write_sysreg ICH_LR(9)
    write_sysreg ICH_LR(10)
    write_sysreg ICH_LR(11)
    write_sysreg ICH_LR(12)
    write_sysreg ICH_LR(13)
    write_sysreg ICH_LR(14)
    write_sysreg ICH_LR(15)

FUNCTION_LABEL(el2_gicv3_done)
    msr vttbr_el2, xzr
    isb
    eret
