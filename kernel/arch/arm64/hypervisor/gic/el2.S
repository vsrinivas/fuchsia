// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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
#define ICH_LR0_EL2(x)      S3_4_C12_C12_ ## x
#define ICH_LR8_EL2(x)      S3_4_C12_C13_ ## x

#define ICH_LR0             ICH_LR0_EL2(0)
#define ICH_LR1             ICH_LR0_EL2(1)
#define ICH_LR2             ICH_LR0_EL2(2)
#define ICH_LR3             ICH_LR0_EL2(3)
#define ICH_LR4             ICH_LR0_EL2(4)
#define ICH_LR5             ICH_LR0_EL2(5)
#define ICH_LR6             ICH_LR0_EL2(6)
#define ICH_LR7             ICH_LR0_EL2(7)
#define ICH_LR8             ICH_LR8_EL2(0)
#define ICH_LR9             ICH_LR8_EL2(1)
#define ICH_LR10            ICH_LR8_EL2(2)
#define ICH_LR11            ICH_LR8_EL2(3)
#define ICH_LR12            ICH_LR8_EL2(4)
#define ICH_LR13            ICH_LR8_EL2(5)
#define ICH_LR14            ICH_LR8_EL2(6)
#define ICH_LR15            ICH_LR8_EL2(7)

#define ICH_LR(x)           ICH_LR ## x

#define READ_SYSREG         0
#define WRITE_SYSREG        1

#define ICH_APR_EL2_ID      0
#define ICH_HCR_EL2_ID      1
#define ICH_VTR_EL2_ID      2
#define ICH_MISR_EL2_ID     3
#define ICH_ELRSR_EL2_ID    4
#define ICH_VMCR_EL2_ID     5
#define ICH_LR_EL2_ID       6

.section .text.el2,"ax",@progbits
.align 12

// Read or write the register.
reg_rw  .req x11
// ID of the register.
reg_id  .req x12
// Number of the register (for APRs or LRs).
reg_num .req x13
// Group of the register (for APRs).
reg_grp .req x14

.macro entry_read_sysreg literal
    mrs x0, \literal
    b el2_gicv3_done
.endm

.macro entry_write_sysreg literal
    msr \literal, x0
    b el2_gicv3_done
.endm

.macro entry_branch_to label
    b \label
    nop
.endm

.macro entry_invalid
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

FUNCTION_LABEL(el2_hvc_sysreg)
    cbnz reg_rw, .Lwrite
    gic_jump .Lread_sys reg_id

.Lread_sys:
    entry_branch_to el2_gicv3_read_apr
    entry_read_sysreg ICH_HCR_EL2
    entry_read_sysreg ICH_VTR_EL2
    entry_read_sysreg ICH_MISR_EL2
    entry_read_sysreg ICH_ELRSR_EL2
    entry_read_sysreg ICH_VMCR_EL2
    entry_branch_to el2_gicv3_read_lr

.Lwrite:
    gic_jump .Lwrite_sys reg_id

.Lwrite_sys:
    entry_branch_to el2_gicv3_write_apr
    entry_write_sysreg ICH_HCR_EL2
    entry_invalid   // ICH_VTR_EL2 is read-only.
    entry_invalid   // ICH_MISR_EL2 is read-only.
    entry_invalid   // ICH_ELRSR_EL2 is read-only.
    entry_write_sysreg ICH_VMCR_EL2
    entry_branch_to el2_gicv3_write_lr

// uint32_t arm64_el2_gicv3_read_gich_apr(uint32_t idx, uint8_t grp)
FUNCTION(arm64_el2_gicv3_read_gich_apr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_APR_EL2_ID
    mov reg_num, x0
    mov reg_grp, x1
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_apr)

// void arm64_el2_gicv3_write_gich_apr(uint32_t val, uint32_t idx, uint8_t grp)
FUNCTION(arm64_el2_gicv3_write_gich_apr)
    mov reg_rw, WRITE_SYSREG
    mov reg_id, ICH_APR_EL2_ID
    mov reg_num, x1
    mov reg_grp, x2
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_apr)

FUNCTION_LABEL(el2_gicv3_read_apr)
    cbnz reg_grp, .Lread_grp1
    gic_jump .Lap0r_read_table reg_num

.Lap0r_read_table:
    entry_read_sysreg ICH_AP0R(0)
    entry_read_sysreg ICH_AP0R(1)
    entry_read_sysreg ICH_AP0R(2)
    entry_read_sysreg ICH_AP0R(3)

.Lread_grp1:
    gic_jump .Lap1r_read_table reg_num

.Lap1r_read_table:
    entry_read_sysreg ICH_AP1R(0)
    entry_read_sysreg ICH_AP1R(1)
    entry_read_sysreg ICH_AP1R(2)
    entry_read_sysreg ICH_AP1R(3)

FUNCTION_LABEL(el2_gicv3_write_apr)
    cbnz reg_grp, .Lwrite_grp1
    gic_jump .Lap0r_write_table reg_num

.Lap0r_write_table:
    entry_write_sysreg ICH_AP0R(0)
    entry_write_sysreg ICH_AP0R(1)
    entry_write_sysreg ICH_AP0R(2)
    entry_write_sysreg ICH_AP0R(3)

.Lwrite_grp1:
    gic_jump .Lap1r_write_table reg_num

.Lap1r_write_table:
    entry_write_sysreg ICH_AP1R(0)
    entry_write_sysreg ICH_AP1R(1)
    entry_write_sysreg ICH_AP1R(2)
    entry_write_sysreg ICH_AP1R(3)

// uint32_t arm64_el2_gicv3_read_gich_hcr()
FUNCTION(arm64_el2_gicv3_read_gich_hcr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_HCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_hcr)

// void arm64_el2_gicv3_write_gich_hcr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_hcr)
    mov reg_rw, WRITE_SYSREG
    mov reg_id, ICH_HCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_hcr)

// uint32_t arm64_el2_gicv3_read_gich_vtr()
FUNCTION(arm64_el2_gicv3_read_gich_vtr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_VTR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_vtr)

// uint32_t arm64_el2_gicv3_read_gich_vmcr()
FUNCTION(arm64_el2_gicv3_read_gich_vmcr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_VMCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_vmcr)

// void arm64_el2_gicv3_write_gich_vmcr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_vmcr)
    mov reg_rw, WRITE_SYSREG
    mov reg_id, ICH_VMCR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_vmcr)

// uint32_t arm64_el2_gicv3_read_gich_elrsr()
FUNCTION(arm64_el2_gicv3_read_gich_elrsr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_ELRSR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_elrsr)

// uint32_t arm64_el2_gicv3_read_gich_misr()
FUNCTION(arm64_el2_gicv3_read_gich_misr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_MISR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_misr)

// uint64_t arm64_el2_gicv3_read_gich_lr(uint32_t idx)
FUNCTION(arm64_el2_gicv3_read_gich_lr)
    mov reg_rw, READ_SYSREG
    mov reg_id, ICH_LR_EL2_ID
    mov reg_num, x0
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_lr)

// void arm64_el2_gicv3_write_gich_lr(uint64_t val, uint32_t idx)
FUNCTION(arm64_el2_gicv3_write_gich_lr)
    mov reg_rw, WRITE_SYSREG
    mov reg_id, ICH_LR_EL2_ID
    mov reg_num, x1
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_lr)

FUNCTION_LABEL(el2_gicv3_read_lr)
    gic_jump .Llr_read_table reg_num

.Llr_read_table:
    entry_read_sysreg ICH_LR(0)
    entry_read_sysreg ICH_LR(1)
    entry_read_sysreg ICH_LR(2)
    entry_read_sysreg ICH_LR(3)
    entry_read_sysreg ICH_LR(4)
    entry_read_sysreg ICH_LR(5)
    entry_read_sysreg ICH_LR(6)
    entry_read_sysreg ICH_LR(7)
    entry_read_sysreg ICH_LR(8)
    entry_read_sysreg ICH_LR(9)
    entry_read_sysreg ICH_LR(10)
    entry_read_sysreg ICH_LR(11)
    entry_read_sysreg ICH_LR(12)
    entry_read_sysreg ICH_LR(13)
    entry_read_sysreg ICH_LR(14)
    entry_read_sysreg ICH_LR(15)

FUNCTION_LABEL(el2_gicv3_write_lr)
    gic_jump .Llr_write_table reg_num

.Llr_write_table:
    entry_write_sysreg ICH_LR(0)
    entry_write_sysreg ICH_LR(1)
    entry_write_sysreg ICH_LR(2)
    entry_write_sysreg ICH_LR(3)
    entry_write_sysreg ICH_LR(4)
    entry_write_sysreg ICH_LR(5)
    entry_write_sysreg ICH_LR(6)
    entry_write_sysreg ICH_LR(7)
    entry_write_sysreg ICH_LR(8)
    entry_write_sysreg ICH_LR(9)
    entry_write_sysreg ICH_LR(10)
    entry_write_sysreg ICH_LR(11)
    entry_write_sysreg ICH_LR(12)
    entry_write_sysreg ICH_LR(13)
    entry_write_sysreg ICH_LR(14)
    entry_write_sysreg ICH_LR(15)

FUNCTION_LABEL(el2_gicv3_done)
    msr vttbr_el2, xzr
    isb
    eret
