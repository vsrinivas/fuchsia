// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/asm_macros.h>
#include <asm.h>
#include <zircon/errors.h>

#define ICH_HCR_EL2                     S3_4_C12_C11_0
#define ICH_VTR_EL2                     S3_4_C12_C11_1
#define ICH_VMCR_EL2                    S3_4_C12_C11_7
#define ICH_ELRS_EL2                    S3_4_C12_C11_5

#define ICH_HCR_EL2_ID                  0
#define ICH_VTR_EL2_ID                  1
#define ICH_VMCR_EL2_ID                 2
#define ICH_ELRS_EL2_ID                 3
#define ICH_LR_EL2_ID                   4

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

.macro switch_to_host
    msr vttbr_el2, xzr
    isb
.endm

.macro exception_return literal
    mov x0, \literal
    eret
.endm

.macro read_sysreg literal
    mrs x0, \literal
    switch_to_host
    exception_return x0
.endm

.macro write_sysreg literal
    msr \literal, x2
    isb
    switch_to_host
    exception_return ZX_OK
.endm

// This macro jumps to the address in the jump table based on
// x3 - Index to calculate the jump address and an offset
// We have two diffrent jump tables with different no. of
// instructions to jump, so the second parameter is to get
// the right offset i.e. no. of bits to left shift.
// base_address_of_table + (x3*16 + x3*4) would be the
// correct address
.macro sysreg_jump table lshift
    adr x9, \table
    lsl x10, x3, 4
    lsl x2, x3, \lshift
    add x10, x10, x2
    add x9, x9, x10
    br x9
.endm

// Each Read or Write to LR register is 2 instructions
// This macro calculates the correct jump address based on the
// x1 - LR index
// base_address_of_table + (x1*8) would be the correct jump address
.macro lr_jump table
    adr x9, \table
    lsl x10, x1, 3
    add x9, x9, x10
    br x9
.endm

.macro read_lr_register reg
    mrs x0, \reg
    b .Llr_done
.endm

.macro write_lr_register reg
    msr \reg, x0
    b .Llr_done
.endm

// x0 - Value to write
// x1 - Index for LR register
// x2 - READ/WRITE
// x3 - Register name/enum
FUNCTION_LABEL(el2_hvc_sysreg)
    cbnz x2, .write_sys

    // .Lread_sys is a jump table but it uses macros
    // Each read is at an offset of 5 instructions
    // The correct jump address for register enum idx is (idx*16 + idx*4)
    // 2nd parameter to the macro is the no. of bits to left shift to calculate
    // the latter part of the math
    sysreg_jump .Lread_sys 2

.Lread_sys:
    read_sysreg ICH_HCR_EL2
    read_sysreg ICH_VTR_EL2
    read_sysreg ICH_VMCR_EL2
    read_sysreg ICH_ELRS_EL2
    b gicv3_read_lr_helper

.write_sys:
    // Similar to the read jump table above
    // Here each write is at an offset of 6 instructions
    // The correct jump address for register enum idx is (idx*16 + idx*8)
    // 2nd parameter to the macro is the no. of bits to left shift to calculate
    // the latter part of the math
    sysreg_jump .Lwrite_sys 3

.Lwrite_sys:
    write_sysreg ICH_HCR_EL2
    write_sysreg ICH_VTR_EL2
    write_sysreg ICH_VMCR_EL2
    write_sysreg ICH_ELRS_EL2
    b gicv3_write_lr_helper

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

// void arm64_el2_gicv3_write_gich_vtr(uint32_t val)
FUNCTION(arm64_el2_gicv3_write_gich_vtr)
    mov x2, WRITE_SYSREG_ID
    mov x3, ICH_VTR_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_write_gich_vtr)

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

// uint32_t arm64_el2_gicv3_read_gich_elrs();
FUNCTION(arm64_el2_gicv3_read_gich_elrs)
    mov x2, READ_SYSREG_ID
    mov x3, ICH_ELRS_EL2_ID
    hvc 5
    ret
END_FUNCTION(arm64_el2_gicv3_read_gich_elrs)

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
FUNCTION_LABEL(gicv3_read_lr_helper)
    lr_jump .Llr_read_table

.Llr_read_table:
    read_lr_register ICH_LR(0)
    read_lr_register ICH_LR(1)
    read_lr_register ICH_LR(2)
    read_lr_register ICH_LR(3)
    read_lr_register ICH_LR(4)
    read_lr_register ICH_LR(5)
    read_lr_register ICH_LR(6)
    read_lr_register ICH_LR(7)
    read_lr_register ICH_LR(8)
    read_lr_register ICH_LR(9)
    read_lr_register ICH_LR(10)
    read_lr_register ICH_LR(11)
    read_lr_register ICH_LR(12)
    read_lr_register ICH_LR(13)
    read_lr_register ICH_LR(14)
    read_lr_register ICH_LR(15)

// x0 - Value to write
// x1 - Index for LR register
FUNCTION_LABEL(gicv3_write_lr_helper)
    lr_jump .Llr_write_table

.Llr_write_table:
    write_lr_register ICH_LR(0)
    write_lr_register ICH_LR(1)
    write_lr_register ICH_LR(2)
    write_lr_register ICH_LR(3)
    write_lr_register ICH_LR(4)
    write_lr_register ICH_LR(5)
    write_lr_register ICH_LR(6)
    write_lr_register ICH_LR(7)
    write_lr_register ICH_LR(8)
    write_lr_register ICH_LR(9)
    write_lr_register ICH_LR(10)
    write_lr_register ICH_LR(11)
    write_lr_register ICH_LR(12)
    write_lr_register ICH_LR(13)
    write_lr_register ICH_LR(14)
    write_lr_register ICH_LR(15)

.Llr_done:
    switch_to_host
    exception_return x0